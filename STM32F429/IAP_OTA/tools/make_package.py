#!/usr/bin/env python3
"""固件打包工具：APP BIN -> Manifest v1 + 校验信息

对应任务书第 6 节的课程自定义格式（32 字节 Manifest v1，小端）+ APP BIN。

用法：
    python make_package.py app_v1.0.bin --ver 1.0.0
    python make_package.py app_v1.1.bin --ver 1.1.0 --flags 1 --out dist/
    python make_package.py app_v1.0.bin --verify        # 只校验并打印字段

产出三样：
    <name>.man     32 字节清单（二进制，直接喂给将来 OTA 的服务器）
    <name>.pkg     清单 + BIN（一个完整包，OTA 下载用）
    <name>.txt     人读的校验信息（贴到报告里）

⚠️ CRC32 必须和 STM32 端逐位一致：
   zlib.crc32 与 core/crc32.c（poly 0xEDB88320，init/xorout 0xFFFFFFFF）完全相同。
   本脚本写完后会自检 "123456789" -> 0xCBF43926。
"""

import argparse
import os
import struct
import sys
import zlib

MAGIC = b"FOTA"
FORMAT_VER = 1
HEADER_LEN = 32
BOARD_F429_V2 = 0x00004292
FLAG_FORCE = 0x00000001

APP_SIZE = 896 * 1024          # core/app_layout.h 里的 APP_SIZE


def ver_to_u32(s):
    """'1.1.0' -> 0x01010000（major<<24 | minor<<16 | build）"""
    parts = (s or "0.0.0").split(".")
    while len(parts) < 3:
        parts.append("0")
    try:
        major, minor, build = (int(p) for p in parts[:3])
    except ValueError:
        raise SystemExit("版本号格式应为 x.y.z，收到: %r" % s)
    for v, name in ((major, "major"), (minor, "minor")):
        if not 0 <= v <= 255:
            raise SystemExit("%s 超出 0..255: %d" % (name, v))
    if not 0 <= build <= 0xFFFF:
        raise SystemExit("build 超出 0..65535: %d" % build)
    return (major << 24) | (minor << 16) | build


def build_manifest(board_id, firmware_ver, image_len, image_crc32, flags):
    """按偏移逐字段拼装，顺序和宽度必须和 core/manifest.c 完全一致"""
    body = struct.pack(
        "<4sHHIIIII",
        MAGIC,            # 0  magic[4]
        FORMAT_VER,       # 4  format_ver  u16
        HEADER_LEN,       # 6  header_len  u16
        board_id,         # 8  board_id    u32
        firmware_ver,     # 12 firmware_ver u32
        image_len,        # 16 image_len   u32
        image_crc32,      # 20 image_crc32 u32
        flags,            # 24 flags       u32
    )
    assert len(body) == HEADER_LEN - 4, len(body)

    header_crc = zlib.crc32(body) & 0xFFFFFFFF   # 只覆盖前 28 字节
    return body + struct.pack("<I", header_crc)


def parse_manifest(raw):
    if len(raw) < HEADER_LEN:
        raise SystemExit("清单不足 32 字节")
    magic, fver, hlen, board, fw, ilen, icrc, flags = struct.unpack_from(
        "<4sHHIIIII", raw, 0)
    hcrc = struct.unpack_from("<I", raw, 28)[0]
    calc = zlib.crc32(raw[:28]) & 0xFFFFFFFF
    return {
        "magic": magic, "format_ver": fver, "header_len": hlen,
        "board_id": board, "firmware_ver": fw, "image_len": ilen,
        "image_crc32": icrc, "flags": flags,
        "header_crc32": hcrc, "header_crc32_calc": calc,
        "header_crc_ok": (hcrc == calc),
    }


def self_test():
    crc = zlib.crc32(b"123456789") & 0xFFFFFFFF
    if crc != 0xCBF43926:
        raise SystemExit("CRC32 自检失败: 0x%08X（应为 0xCBF43926）" % crc)


def main():
    ap = argparse.ArgumentParser(description="生成 Manifest v1 固件包")
    ap.add_argument("bin", help="输入 .bin 文件")
    ap.add_argument("--ver", default="1.0.0", help="固件版本 x.y.z")
    ap.add_argument("--board", default=str(BOARD_F429_V2),
                    help="board_id（十进制或 0x 开头）")
    ap.add_argument("--flags", default="0", help="flags，十进制或 0x 开头（bit0=强制）")
    ap.add_argument("--out", default=".", help="输出目录")
    ap.add_argument("--verify", action="store_true", help="只校验已有 .man")
    args = ap.parse_args()

    self_test()

    # ---- --verify 模式 ----
    if args.verify:
        man_path = os.path.splitext(args.bin)[0] + ".man"
        if not os.path.exists(man_path):
            raise SystemExit("找不到 %s" % man_path)
        raw = open(man_path, "rb").read()
        m = parse_manifest(raw)
        print("magic            : %r" % m["magic"])
        print("format_ver       : %d" % m["format_ver"])
        print("header_len       : %d" % m["header_len"])
        print("board_id         : 0x%08X" % m["board_id"])
        print("firmware_ver     : 0x%08X  (%d.%d.%d)" % (
            m["firmware_ver"],
            (m["firmware_ver"] >> 24) & 0xFF,
            (m["firmware_ver"] >> 16) & 0xFF,
            m["firmware_ver"] & 0xFFFF))
        print("image_len        : %d" % m["image_len"])
        print("image_crc32      : 0x%08X" % m["image_crc32"])
        print("flags            : 0x%08X" % m["flags"])
        print("header_crc32     : 0x%08X (calc 0x%08X) -> %s" % (
            m["header_crc32"], m["header_crc32_calc"],
            "OK" if m["header_crc_ok"] else "MISMATCH"))
        return

    # ---- 打包模式 ----
    if not os.path.exists(args.bin):
        raise SystemExit("找不到 %s" % args.bin)

    image = open(args.bin, "rb").read()
    if len(image) == 0:
        raise SystemExit("BIN 是空的")
    if len(image) > APP_SIZE:
        raise SystemExit("BIN 太大：%d > APP_SIZE(%d)" % (len(image), APP_SIZE))

    board = int(args.board, 0)
    flags = int(args.flags, 0)
    ver = ver_to_u32(args.ver)
    img_crc = zlib.crc32(image) & 0xFFFFFFFF

    man = build_manifest(board, ver, len(image), img_crc, flags)
    assert len(man) == HEADER_LEN

    base = os.path.splitext(os.path.basename(args.bin))[0]
    os.makedirs(args.out, exist_ok=True)

    man_path = os.path.join(args.out, base + ".man")
    pkg_path = os.path.join(args.out, base + ".pkg")
    txt_path = os.path.join(args.out, base + ".txt")

    open(man_path, "wb").write(man)
    open(pkg_path, "wb").write(man + image)

    # 自检：重新解析一遍
    m = parse_manifest(man)
    assert m["header_crc_ok"], "生成的清单 header_crc 自检失败"

    lines = [
        "firmware package",
        "  source bin   : %s" % args.bin,
        "  size         : %d bytes" % len(image),
        "  version      : %s  (0x%08X)" % (args.ver, ver),
        "  board_id     : 0x%08X" % board,
        "  image_crc32  : 0x%08X" % img_crc,
        "  flags        : 0x%08X" % flags,
        "  manifest     : %d bytes, header_crc32 = 0x%08X (ok)" % (
            len(man), m["header_crc32"]),
        "  package      : %s (%d bytes = 32 + %d)" % (
            pkg_path, len(man) + len(image), len(image)),
        "",
        "flash it with:",
        "  YMODEM send %s over the bootloader's UART" % args.bin,
    ]
    open(txt_path, "w", encoding="utf-8", newline="\n").write("\n".join(lines) + "\n")

    print("\n".join(lines))


if __name__ == "__main__":
    main()
