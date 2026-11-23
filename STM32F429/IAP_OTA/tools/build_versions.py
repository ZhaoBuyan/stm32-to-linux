"""构建 APP 的两个版本。

APP v1.0 / v1.1 共用同一份源码，靠宏 APP_V11 区分：
  v1.0  采样 500ms，LED 慢闪，只报 时间+数值
  v1.1  采样 200ms，LED 快闪，多报 版本号+故障计数

流程：
  1. 先按现状编 v1.0（已由 Keil 生成 Build/app_v1.0.bin）
  2. 往 Define 里加 APP_V11，编一次，把产物另存为 Build/app_v1.1.bin
  3. 把 Define 改回去，重编，恢复 v1.0 的产物
  4. 对比两个 bin：必须不一样（否则说明宏没生效）
"""
import hashlib
import io
import os
import re
import shutil
import subprocess
import sys

BASE = r"C:\Users\赵不言\Documents\code\嵌入式文件夹\STM32F429\IAP_OTA"
PROJ = os.path.join(BASE, r"app\MDK-ARM\App.uvprojx")
BUILD = os.path.join(BASE, r"app\MDK-ARM\Build")
UV4 = r"D:\Keil_v5\UV4\UV4.exe"
LOG = r"C:\pc_build\v11.log"

ORIG_DEFINE = "USE_HAL_DRIVER,STM32F429xx"
V11_DEFINE = "USE_HAL_DRIVER,STM32F429xx,APP_V11"


def build():
    if os.path.exists(LOG):
        os.remove(LOG)
    rc = subprocess.call([UV4, "-b", PROJ, "-o", LOG])
    out = ""
    if os.path.exists(LOG):
        out = io.open(LOG, encoding="gbk", errors="replace").read()
    return rc, out


def set_define(value):
    xml = io.open(PROJ, encoding="utf-8").read()
    xml = re.sub(r"<Define>[^<]*</Define>", "<Define>%s</Define>" % value, xml)
    io.open(PROJ, "w", encoding="utf-8", newline="\n").write(xml)


def sha(path):
    return hashlib.sha256(io.open(path, "rb").read()).hexdigest()[:16]


def main():
    v10 = os.path.join(BUILD, "app_v1.0.bin")
    v11 = os.path.join(BUILD, "app_v1.1.bin")

    # ---- 先确保 v1.0 是当前产物 ----
    set_define(ORIG_DEFINE)
    rc, out = build()
    if rc != 0 or not os.path.exists(v10):
        print("v1.0 build failed rc=%d" % rc)
        print(out[-1500:])
        return 1
    size_v10 = os.path.getsize(v10)
    sha_v10 = sha(v10)
    print("v1.0  %6d bytes  sha %s" % (size_v10, sha_v10))

    # ---- 编 v1.1 ----
    set_define(V11_DEFINE)
    rc, out = build()
    if rc != 0:
        print("v1.1 build failed rc=%d" % rc)
        print(out[-1500:])
        set_define(ORIG_DEFINE)
        return 1
    if not os.path.exists(v10):
        print("v1.1 build produced no bin")
        set_define(ORIG_DEFINE)
        return 1
    shutil.copyfile(v10, v11)
    size_v11 = os.path.getsize(v11)
    sha_v11 = sha(v11)
    print("v1.1  %6d bytes  sha %s" % (size_v11, sha_v11))

    # ---- 恢复 v1.0 ----
    set_define(ORIG_DEFINE)
    rc, out = build()
    print("restored v1.0 build rc=%d" % rc)

    # ---- 两个 bin 必须不同 ----
    if sha_v10 == sha_v11:
        print("!! 两个 bin 完全相同 —— APP_V11 宏没生效")
        return 1
    print("OK: v1.0 和 v1.1 的产物不同（%s vs %s）" % (sha_v10, sha_v11))
    return 0


if __name__ == "__main__":
    sys.exit(main())
