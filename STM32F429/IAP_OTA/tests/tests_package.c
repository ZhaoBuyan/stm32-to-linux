/**
 ******************************************************************************
 * @file    tests_package.c
 * @brief   PC/MCU 互验测试：用**和 STM32 完全相同的 C 代码**校验 Python 打的包
 *
 * 为什么要单独做这个测试：
 *   打包工具是 Python（用 zlib.crc32），设备端是 C（core/crc32.c）。
 *   如果两边的 CRC 参数有任何差别（初始值、异或、覆盖范围），
 *   在 PC 上永远自洽，一上板就"校验失败"，而且极难看出原因。
 *
 *   所以这里用 C 侧的实现去解析 Python 生成的 .pkg，
 *   两边结果一致才算真的对上了。
 *
 * 用法：
 *   tests_package.exe dist/app_v1.0.pkg
 *
 *  返回 0 = 通过
 ******************************************************************************
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_layout.h"
#include "crc32.h"
#include "manifest.h"

static int g_pass = 0;
static int g_fail = 0;

static void check(int cond, const char *name)
{
    if (cond) { g_pass++; printf("  PASS  %s\n", name); }
    else      { g_fail++; printf("  FAIL  %s  <--\n", name); }
}

static uint8_t *read_file(const char *path, long *out_len)
{
    FILE    *fp;
    uint8_t *buf;
    long     len;

    fp = fopen(path, "rb");
    if (fp == NULL) { return NULL; }

    fseek(fp, 0, SEEK_END);
    len = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (len <= 0) { fclose(fp); return NULL; }

    buf = (uint8_t *)malloc((size_t)len);
    if (buf == NULL) { fclose(fp); return NULL; }

    if (fread(buf, 1, (size_t)len, fp) != (size_t)len)
    {
        free(buf);
        fclose(fp);
        return NULL;
    }

    fclose(fp);
    *out_len = len;
    return buf;
}

int main(int argc, char **argv)
{
    uint8_t      *pkg;
    long          pkg_len;
    Manifest      m;
    ManifestResult mr;
    uint32_t      crc_image;
    char          vs[16];

    printf("== package interop test ==\n");

    if (argc < 2)
    {
        printf("usage: %s <file.pkg>\n", argv[0]);
        return 2;
    }

    pkg = read_file(argv[1], &pkg_len);
    if (pkg == NULL)
    {
        printf("  cannot read %s\n", argv[1]);
        return 2;
    }

    printf("  package: %s  (%ld bytes)\n", argv[1], pkg_len);

    /* ① 包至少要有清单 */
    check(pkg_len > (long)MANIFEST_SIZE, "package larger than manifest");

    if (pkg_len <= (long)MANIFEST_SIZE) { return 1; }

    /* ② 用 C 侧解析器读清单 */
    mr = Manifest_Parse(pkg, &m, MANIFEST_BOARD_F429);
    printf("  manifest: %s\n", Manifest_ResultName(mr));
    check(mr == MF_OK, "C parser accepts the python-built manifest");

    if (mr != MF_OK) { printf("\n%d passed, %d failed\n", g_pass, g_fail); return 1; }

    /* ③ 清单字段自洽 */
    check(m.format_ver == MANIFEST_FORMAT_VER, "format_ver = 1");
    check(m.header_len == MANIFEST_SIZE,       "header_len = 32");
    check(m.board_id == MANIFEST_BOARD_F429,   "board_id f429 v2");

    Manifest_VersionStr(m.firmware_ver, vs, (int)sizeof(vs));
    printf("  version : %s\n", vs);

    /* ④ 镜像长度必须等于"包长 - 32" */
    check((long)m.image_len == pkg_len - (long)MANIFEST_SIZE,
          "image_len matches package size - 32");

    /* ⑤ 关键：C 侧 CRC32 必须和 Python 写进清单的值相同 */
    crc_image = Crc32_Calc(pkg + MANIFEST_SIZE, m.image_len);
    printf("  image crc: manifest 0x%08lX / C-calc 0x%08lX\n",
           (unsigned long)m.image_crc32, (unsigned long)crc_image);
    check(crc_image == m.image_crc32, "C crc32 == python crc32 (interop)");

    /* ⑥ 再确认清单自身的 header_crc 也对 */
    check(Manifest_HeaderCrcOk(pkg) == 1, "header crc ok on C side");

    /* ⑦ 镜像长度要在 APP 区容量以内 */
    check(m.image_len <= APP_SIZE, "image fits in APP area");
    check(App_IsRangeInApp(APP_ADDR, m.image_len) == 1,
          "image range valid against APP layout");

    free(pkg);

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    printf("=> PC(python) and MCU(C) agree on crc32 and manifest layout\n");
    return (g_fail == 0) ? 0 : 1;
}
