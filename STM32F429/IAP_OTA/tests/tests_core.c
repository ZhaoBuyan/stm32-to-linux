/**
 ******************************************************************************
 * @file    tests_core.c
 * @brief   IAP/OTA 核心逻辑的 PC 端单元测试
 *
 * 编译运行：
 *   gcc -std=c11 -Wall -Wextra -Icore tests/tests_core.c \
 *       core/crc32.c core/app_layout.c core/manifest.c core/ymodem.c -o tests_core.exe
 *   ./tests_core.exe
 *
 * 覆盖四块纯逻辑（都不碰硬件，所以能在这里验证完）：
 *   CRC32         与 zlib 标准向量对拍 + 增量与一次性一致
 *   分区与入口     MSP / Thumb / 范围 / 溢出 / 零长度
 *   Manifest      正常构建解析 + 8 种错误
 *   YMODEM        正常流程 / 重复包 / 坏 CRC / 坏序号 / CAN / 超长
 ******************************************************************************
 */
#include <stdio.h>
#include <string.h>

#include "app_layout.h"
#include "crc32.h"
#include "manifest.h"
#include "ymodem.h"

static int g_pass = 0;
static int g_fail = 0;

static void check(int cond, const char *name)
{
    if (cond) { g_pass++; printf("  PASS  %s\n", name); }
    else      { g_fail++; printf("  FAIL  %s  <--\n", name); }
}

/* ============================================================================
 * 一、CRC32
 * ========================================================================== */
static void test_crc32(void)
{
    const char *v = "123456789";
    uint32_t     crc;

    printf("\n-- Crc32 --\n");

    /* zlib.crc32(b"123456789") == 0xCBF43926，这是公开标准向量 */
    check(Crc32_Calc(v, 9) == 0xCBF43926u, "\"123456789\" -> 0xCBF43926");
    check(Crc32_Calc("", 0) == 0x00000000u, "empty -> 0x00000000");
    check(Crc32_Calc("a", 1) == 0xE8B7BE43u, "\"a\" -> 0xE8B7BE43");

    /* 增量计算必须和一次性结果相同 */
    crc = Crc32_Start();
    crc = Crc32_Update(crc, v, 4);
    crc = Crc32_Update(crc, v + 4, 5);
    crc = Crc32_Finish(crc);
    check(crc == 0xCBF43926u, "incremental == one-shot");

    /* NULL 输入不能崩 */
    check(Crc32_Calc(NULL, 10) == 0x00000000u, "NULL is safe");
}

/* ============================================================================
 * 二、分区与 APP 入口有效性
 * ========================================================================== */
static void test_app_layout(void)
{
    printf("\n-- app_layout --\n");

    /* --- MSP --- */
    check(App_IsMspValid(0x2002FF00u, 0) == 1, "MSP inside SRAM ok");
    check(App_IsMspValid(0x20000000u, 0) == 1, "MSP at SRAM base ok");
    check(App_IsMspValid(0x20030000u, 0) == 1, "MSP at SRAM end ok");
    check(App_IsMspValid(0x20040000u, 0) == 0, "MSP beyond SRAM rejected");
    check(App_IsMspValid(0x08040000u, 0) == 0, "MSP in flash rejected");
    check(App_IsMspValid(0xFFFFFFFFu, 0) == 0, "MSP 0xFFFFFFFF (blank) rejected");
    check(App_IsMspValid(0x2002FF02u, 0) == 0, "MSP not 4-byte aligned rejected");
    check(App_IsMspValid(0x10001000u, 1) == 1, "MSP in CCM ok when allowed");
    check(App_IsMspValid(0x10001000u, 0) == 0, "MSP in CCM rejected when not allowed");

    /* --- 入口地址 --- */
    check(App_IsEntryValid(0x08020101u) == 1, "thumb entry inside APP ok");
    check(App_IsEntryValid(0x08020100u) == 0, "even entry (no thumb bit) rejected");
    check(App_IsEntryValid(0x08000101u) == 0, "entry in bootloader rejected");
    check(App_IsEntryValid(0x08100101u) == 0, "entry past APP end rejected");
    check(App_IsEntryValid(0xFFFFFFFFu) == 0, "blank entry rejected");

    /* --- 综合判断 --- */
    check(App_CheckVector(0x2002FF00u, 0x08020101u) == APP_IMG_OK, "vector ok");
    check(App_CheckVector(0xFFFFFFFFu, 0x08020101u) == APP_IMG_ERR_EMPTY, "blank MSP -> EMPTY");
    check(App_CheckVector(0x2002FF00u, 0xFFFFFFFFu) == APP_IMG_ERR_EMPTY, "blank entry -> EMPTY");
    check(App_CheckVector(0x2002FF00u, 0x08020100u) == APP_IMG_ERR_ENTRY_THUMB,
          "no thumb bit -> ENTRY_THUMB");
    check(App_CheckVector(0x40000000u, 0x08020101u) == APP_IMG_ERR_MSP, "bad MSP -> ERR_MSP");
    check(App_CheckVector(0x2002FF00u, 0x08000101u) == APP_IMG_ERR_ENTRY_RANGE,
          "entry in boot -> ENTRY_RANGE");

    /* --- 擦写范围 --- */
    check(App_IsRangeInApp(APP_ADDR, 4096u) == 1, "range at APP start ok");
    check(App_IsRangeInApp(APP_END_ADDR - 4096u, 4096u) == 1, "range at APP end ok");
    check(App_IsRangeInApp(APP_ADDR, 0u) == 0, "zero length rejected");
    check(App_IsRangeInApp(BOOT_ADDR, 4096u) == 0, "range in bootloader rejected");
    check(App_IsRangeInApp(APP_END_ADDR, 1u) == 0, "range past APP end rejected");
    check(App_IsRangeInApp(0xFFFFFF00u, 0x200u) == 0, "address overflow rejected");
    check(App_IsRangeInApp(APP_END_ADDR - 1u, 2u) == 0, "range crossing end rejected");
}

/* ============================================================================
 * 三、Manifest
 * ========================================================================== */
static void test_manifest(void)
{
    uint8_t      raw[MANIFEST_SIZE];
    Manifest     m;
    char         vs[16];
    const uint32_t ver = 0x01010000u;   /* 1.1.0 */

    printf("\n-- Manifest --\n");

    Manifest_Build(raw, MANIFEST_BOARD_F429, ver, 0x3000u, 0xDEADBEEFu, 0u);

    check(Manifest_Parse(raw, &m, MANIFEST_BOARD_F429) == MF_OK, "parse built manifest");
    check(m.format_ver == 1u, "format_ver = 1");
    check(m.header_len == 32u, "header_len = 32");
    check(m.board_id == MANIFEST_BOARD_F429, "board_id round-trip");
    check(m.firmware_ver == ver, "firmware_ver round-trip");
    check(m.image_len == 0x3000u, "image_len round-trip");
    check(m.image_crc32 == 0xDEADBEEFu, "image_crc32 round-trip");
    check(Manifest_HeaderCrcOk(raw) == 1, "header crc ok");

    Manifest_VersionStr(ver, vs, (int)sizeof(vs));
    check(strcmp(vs, "1.1.0") == 0, "version string 1.1.0");
    Manifest_VersionStr(0x01000000u, vs, (int)sizeof(vs));
    check(strcmp(vs, "1.0.0") == 0, "version string 1.0.0");

    /* --- 各种错误 --- */
    {
        uint8_t bad[MANIFEST_SIZE];

        memcpy(bad, raw, sizeof(bad));
        bad[0] = 'X';
        check(Manifest_Parse(bad, &m, MANIFEST_BOARD_F429) == MF_ERR_MAGIC, "bad magic");

        memcpy(bad, raw, sizeof(bad));
        bad[4] = 9;
        check(Manifest_Parse(bad, &m, MANIFEST_BOARD_F429) == MF_ERR_FORMAT_VER,
              "bad format_ver");

        memcpy(bad, raw, sizeof(bad));
        bad[6] = 40;
        check(Manifest_Parse(bad, &m, MANIFEST_BOARD_F429) == MF_ERR_HEADER_LEN,
              "bad header_len");

        /* 改一个字段但不改 CRC -> 校验失败 */
        memcpy(bad, raw, sizeof(bad));
        bad[12] ^= 0xFFu;
        check(Manifest_Parse(bad, &m, MANIFEST_BOARD_F429) == MF_ERR_HEADER_CRC,
              "corrupted field -> header crc mismatch");

        check(Manifest_Parse(raw, &m, 0x12345678u) == MF_ERR_BOARD, "wrong board id");
        check(Manifest_Parse(raw, &m, 0u) == MF_OK, "board check skipped when 0");

        /* image_len = 0 */
        Manifest_Build(bad, MANIFEST_BOARD_F429, ver, 0u, 0x11111111u, 0u);
        check(Manifest_Parse(bad, &m, MANIFEST_BOARD_F429) == MF_ERR_IMAGE_LEN,
              "image_len 0 rejected");

        /* image_len 超过 APP 区 */
        Manifest_Build(bad, MANIFEST_BOARD_F429, ver, APP_SIZE + 1u, 0x11111111u, 0u);
        check(Manifest_Parse(bad, &m, MANIFEST_BOARD_F429) == MF_ERR_IMAGE_LEN,
              "image_len > APP_SIZE rejected");

        /* image_len 正好等于 APP_SIZE 应该可以 */
        Manifest_Build(bad, MANIFEST_BOARD_F429, ver, APP_SIZE, 0x11111111u, 0u);
        check(Manifest_Parse(bad, &m, MANIFEST_BOARD_F429) == MF_OK,
              "image_len == APP_SIZE accepted");

        /* image_crc32 = 0 */
        Manifest_Build(bad, MANIFEST_BOARD_F429, ver, 0x100u, 0u, 0u);
        check(Manifest_Parse(bad, &m, MANIFEST_BOARD_F429) == MF_ERR_IMAGE_CRC_ZERO,
              "image_crc32 0 rejected");

        /* 全 0xFF（空片）应报 magic 错 */
        memset(bad, 0xFF, sizeof(bad));
        check(Manifest_Parse(bad, &m, MANIFEST_BOARD_F429) == MF_ERR_MAGIC,
              "all 0xFF (blank) -> bad magic");

        /* flags 位 */
        Manifest_Build(bad, MANIFEST_BOARD_F429, ver, 0x100u, 0x22222222u,
                       MANIFEST_FLAG_FORCE);
        check(Manifest_Parse(bad, &m, MANIFEST_BOARD_F429) == MF_OK &&
              (m.flags & MANIFEST_FLAG_FORCE) != 0u, "force flag round-trip");
    }
}

/* ============================================================================
 * 四、YMODEM
 * ========================================================================== */

/* 收集写入的字节，供断言 */
static uint8_t  g_out[4096];
static uint32_t g_out_len;

static int test_write(void *user, const uint8_t *data, uint32_t len)
{
    (void)user;

    if (g_out_len + len > sizeof(g_out)) { return -1; }
    memcpy(&g_out[g_out_len], data, len);
    g_out_len += len;
    return 0;
}

/* 组装一个 YMODEM 包，返回总长度 */
static int build_pkt(uint8_t *out, uint8_t seq, const uint8_t *data, uint16_t len)
{
    uint16_t crc;

    out[0] = (len == 1024u) ? 0x02u : 0x01u;
    out[1] = seq;
    out[2] = (uint8_t)(~seq);

    if (len > 0u && data != 0) { memcpy(&out[3], data, len); }
    else if (len > 0u)         { memset(&out[3], 0, len); }

    crc = Ym_Crc16(&out[3], len);
    out[3 + len] = (uint8_t)(crc >> 8);
    out[4 + len] = (uint8_t)(crc & 0xFF);

    return (int)(len + 5u);
}

/* 把一整块字节喂给状态机，返回最后一次非 NONE 的应答 */
static YmReply feed_all(YmCtx *ctx, const uint8_t *p, int n, int *saw_abort)
{
    YmReply last = YM_REPLY_NONE;
    int i;

    for (i = 0; i < n; i++)
    {
        YmReply r = Ym_Feed(ctx, p[i]);

        if (r == YM_REPLY_ABORT) { if (saw_abort) { *saw_abort = 1; } }
        if (r != YM_REPLY_NONE)  { last = r; }
    }
    return last;
}

static void test_ymodem(void)
{
    YmCtx    ctx;
    uint8_t  pkt[1400];
    uint8_t  payload[1024];
    int      n, abort_flag;

    printf("\n-- Ymodem --\n");

    /* ---------- 正常流程：文件头 + 2 个数据包 + EOT ---------- */
    memset(g_out, 0, sizeof(g_out));
    g_out_len = 0;
    Ym_Init(&ctx, test_write, NULL, APP_SIZE);

    /* 文件头：文件名 "app.bin" + 大小 0o1000(=512 十进制) */
    memset(payload, 0, sizeof(payload));
    strcpy((char *)payload, "app.bin");
    strcpy((char *)payload + 8, "1000");
    n = build_pkt(pkt, 0, payload, 128);
    abort_flag = 0;
    check(feed_all(&ctx, pkt, n, &abort_flag) == YM_REPLY_ACK, "header packet -> ACK");
    check(strcmp(Ym_FileName(&ctx), "app.bin") == 0, "filename parsed");
    check(Ym_FileSize(&ctx) == 512u, "file size 0o1000 = 512");
    check(Ym_BytesWritten(&ctx) == 0u, "header does not write data");

    /* 数据包 1（128 字节全 0x11）*/
    memset(payload, 0x11, sizeof(payload));
    n = build_pkt(pkt, 1, payload, 128);
    check(feed_all(&ctx, pkt, n, &abort_flag) == YM_REPLY_ACK, "data pkt 1 -> ACK");
    check(Ym_BytesWritten(&ctx) == 128u, "128 bytes written");

    /* 数据包 2（1024 字节，全 0x22）*/
    memset(payload, 0x22, sizeof(payload));
    n = build_pkt(pkt, 2, payload, 1024);
    check(feed_all(&ctx, pkt, n, &abort_flag) == YM_REPLY_ACK, "1024-byte pkt 2 -> ACK");
    check(Ym_BytesWritten(&ctx) == 1152u, "total 1152 bytes");
    check(g_out_len == 1152u, "callback received all bytes");
    check(g_out[0] == 0x11u && g_out[127] == 0x11u, "first block content");
    check(g_out[128] == 0x22u && g_out[1151] == 0x22u, "second block content");

    /* EOT */
    check(Ym_Feed(&ctx, 0x04) == YM_REPLY_ACK, "EOT -> ACK");
    check(ctx.state == YM_ST_DONE, "state DONE after EOT");
    check(Ym_Feed(&ctx, 0x04) == YM_REPLY_DONE, "further bytes keep DONE");

    /* ---------- 重复包：ACK 但不重复写 ---------- */
    memset(g_out, 0, sizeof(g_out));
    g_out_len = 0;
    Ym_Init(&ctx, test_write, NULL, APP_SIZE);

    memset(payload, 0, sizeof(payload));
    strcpy((char *)payload, "a.bin");
    strcpy((char *)payload + 8, "10000");
    n = build_pkt(pkt, 0, payload, 128);
    feed_all(&ctx, pkt, n, NULL);

    memset(payload, 0xAA, sizeof(payload));
    n = build_pkt(pkt, 1, payload, 128);
    feed_all(&ctx, pkt, n, NULL);
    check(Ym_BytesWritten(&ctx) == 128u, "duplicate test: first write ok");

    /* 同一个包再发一次（模拟 ACK 丢失后的重发）*/
    check(feed_all(&ctx, pkt, n, NULL) == YM_REPLY_ACK, "duplicate pkt -> ACK");
    check(Ym_BytesWritten(&ctx) == 128u, "duplicate pkt NOT written again");
    check(g_out_len == 128u, "callback not called twice");

    /* 之后正常的下一包仍然能收 */
    memset(payload, 0xBB, sizeof(payload));
    n = build_pkt(pkt, 2, payload, 128);
    check(feed_all(&ctx, pkt, n, NULL) == YM_REPLY_ACK, "next pkt after duplicate ok");
    check(Ym_BytesWritten(&ctx) == 256u, "bytes after duplicate correct");

    /* ---------- 坏 CRC ---------- */
    Ym_Init(&ctx, test_write, NULL, APP_SIZE);
    memset(g_out, 0, sizeof(g_out));
    g_out_len = 0;

    memset(payload, 0, sizeof(payload));
    strcpy((char *)payload, "b.bin");
    strcpy((char *)payload + 8, "10000");
    n = build_pkt(pkt, 0, payload, 128);
    feed_all(&ctx, pkt, n, NULL);

    memset(payload, 0x55, sizeof(payload));
    n = build_pkt(pkt, 1, payload, 128);
    pkt[n - 1] ^= 0xFFu;                    /* 破坏 CRC 低字节 */
    check(feed_all(&ctx, pkt, n, NULL) == YM_REPLY_NAK, "bad CRC -> NAK");
    check(Ym_BytesWritten(&ctx) == 0u, "bad CRC does not write");

    /* ---------- 序号反码错误 ---------- */
    Ym_Init(&ctx, test_write, NULL, APP_SIZE);
    memset(g_out, 0, sizeof(g_out));
    g_out_len = 0;
    memset(payload, 0, sizeof(payload));
    strcpy((char *)payload, "c.bin");
    strcpy((char *)payload + 8, "10000");
    n = build_pkt(pkt, 0, payload, 128);
    feed_all(&ctx, pkt, n, NULL);

    memset(payload, 0x66, sizeof(payload));
    n = build_pkt(pkt, 1, payload, 128);
    pkt[2] = 0x00u;                         /* ~seq 故意写错 */
    check(feed_all(&ctx, pkt, n, NULL) == YM_REPLY_NAK, "bad ~seq -> NAK");
    check(Ym_BytesWritten(&ctx) == 0u, "bad ~seq does not write");

    /* ---------- CAN 取消 ---------- */
    Ym_Init(&ctx, test_write, NULL, APP_SIZE);
    check(Ym_Feed(&ctx, 0x18) == YM_REPLY_ABORT, "CAN -> ABORT");
    check(ctx.state == YM_ST_ABORT, "state ABORT after CAN");

    /* ---------- 超过 max_size 必须中止（安全红线）---------- */
    memset(g_out, 0, sizeof(g_out));
    g_out_len = 0;
    Ym_Init(&ctx, test_write, NULL, 200u);  /* 只允许 200 字节 */

    memset(payload, 0, sizeof(payload));
    strcpy((char *)payload, "d.bin");
    strcpy((char *)payload + 8, "10000");
    n = build_pkt(pkt, 0, payload, 128);
    feed_all(&ctx, pkt, n, NULL);

    /* 第 1 包 128 字节：还在 200 以内，应该正常写入 */
    memset(payload, 0x77, sizeof(payload));
    n = build_pkt(pkt, 1, payload, 128);
    check(feed_all(&ctx, pkt, n, NULL) == YM_REPLY_ACK, "pkt1 within max_size ok");
    check(Ym_BytesWritten(&ctx) == 128u, "pkt1 written");

    /* 第 2 包再加 128 = 256 > 200，必须中止 */
    n = build_pkt(pkt, 2, payload, 128);
    abort_flag = 0;
    feed_all(&ctx, pkt, n, &abort_flag);
    check(abort_flag == 1, "writing past max_size -> ABORT");
    check(ctx.state == YM_ST_ABORT, "state ABORT after overflow");
    check(Ym_BytesWritten(&ctx) == 128u, "no bytes written past the limit");

    /* ---------- IDLE 里的垃圾字节被忽略 ---------- */
    Ym_Init(&ctx, test_write, NULL, APP_SIZE);
    check(Ym_Feed(&ctx, 0x7F) == YM_REPLY_NONE, "garbage byte ignored");
    check(Ym_Feed(&ctx, 0x00) == YM_REPLY_NONE, "zero byte ignored");
    check(ctx.state == YM_ST_IDLE, "still IDLE after garbage");

    /* ---------- CRC16 已知值 ---------- */
    /* CRC16-XMODEM("123456789") = 0x31C3 */
    check(Ym_Crc16((const uint8_t *)"123456789", 9) == 0x31C3u,
          "CRC16-XMODEM(\"123456789\") = 0x31C3");
}

int main(void)
{
    printf("== IAP/OTA core tests ==\n");

    test_crc32();
    test_app_layout();
    test_manifest();
    test_ymodem();

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
