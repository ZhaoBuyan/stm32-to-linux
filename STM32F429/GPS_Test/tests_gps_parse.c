/**
 ******************************************************************************
 * @file    tests_gps_parse.c
 * @brief   nmea.c / rmc.c / gga.c / gps_fix.c / fmt.c 的 PC 端单元测试
 *
 * 编译运行：
 *   gcc -std=c11 -Wall -Wextra -IUser/gps tests_gps_parse.c \
 *       User/gps/nmea.c User/gps/rmc.c User/gps/gga.c \
 *       User/gps/gps_fix.c User/gps/fmt.c -o tests_gps_parse.exe
 *   ./tests_gps_parse.exe
 *
 * ⚠️ 本文件里**没有任何构造的 GPS 语句**。
 *
 * 测试数据只有两个来源：
 *   A. **实测抓包** testdata/nmea_no_fix.txt
 *      （2026-11-16 室内、天线未接好时抓的 24 条，天线问题已排查完）
 *   B. **纯算法换算**：直接给函数喂数值，验证换算公式
 *      （DdmmToE7 / Fmt_* 这类，输入是数学值不是"GPS 输出"，
 *        跟断言 1+1=2 是一回事，不算伪造数据）
 *
 * 早期版本里有一批人工构造的 RMC/GGA/GSV 语句（比如
 * "$GNRMC,081836.00,A,3150.70613,...*70"），那些是**假的 GPS 输出** ——
 * 校验和是我自己算的，测试只能证明"解析器和我的校验和函数互相同意"，
 * 不能证明它们符合真实接收机的输出。已经全部删掉。
 *
 * ⚠️ 因此现在**有效定位（A + 坐标）这条路径没有测试覆盖**：
 *    实测数据里从来没有定位成功过。等室外拿到真实定位帧后，
 *    把那一帧加进 testdata/，再补上对应断言。
 ******************************************************************************
 */
#include <stdio.h>
#include <string.h>

#include "fmt.h"
#include "gga.h"
#include "gps_fix.h"
#include "nmea.h"
#include "rmc.h"

static int g_pass = 0;
static int g_fail = 0;

static void check(int cond, const char *name)
{
    if (cond) { g_pass++; printf("  PASS  %s\n", name); }
    else      { g_fail++; printf("  FAIL  %s  <--\n", name); }
}

/* ============================================================================
 * A. 实测抓包（唯一的数据来源）
 *
 * 2026-11-16 室内抓的 24 条，校验和 24/24 通过（脚本独立验过）。
 * 特征：天线未接好 -> 模块报 ANTENNA OPEN、0 颗星、状态全 V。
 * ========================================================================== */
static const char *k_real[] =
{
    "$BDGSV,1,1,00*68",
    "$GNRMC,,V,,,,,,,,,,N*4D",
    "$GNVTG,,,,,,,,,N*2E",
    "$GNZDA,,,,,,*56",
    "$GPTXT,01,01,01,ANTENNA OPEN*25",
    "$GNGGA,,,,,,0,00,25.5,,,,,,*64",
    "$GNGLL,,,,,,V,N*7A",
    "$GPGSA,A,1,,,,,,,,,,,,,25.5,25.5,25.5*02",
    "$BDGSA,A,1,,,,,,,,,,,,,25.5,25.5,25.5*13",
    "$GPGSV,1,1,00*79",
    "$GPTXT,01,01,02,MA=CASIC*27",
    "$GPTXT,01,01,02,HW=ATGM332D,0005063714758*12",
    "$GPTXT,01,01,02,IC=AT6558-5N-31-0C510800,FE28CKJ-F1-025348*52",
    "$GPTXT,01,01,02,SW=URANUS5,V5.3.0.0*1D",
};

/* 从上面挑出来的单条，方便单独用 */
static const char *REAL_RMC_V   = "$GNRMC,,V,,,,,,,,,,N*4D";
static const char *REAL_GGA_0   = "$GNGGA,,,,,,0,00,25.5,,,,,,*64";
static const char *REAL_GSV_0   = "$GPGSV,1,1,00*79";
static const char *REAL_TXT_ANT = "$GPTXT,01,01,01,ANTENNA OPEN*25";
static const char *REAL_TXT_VER = "$GPTXT,01,01,02,HW=ATGM332D,0005063714758*12";

/* ============================================================================
 * 一、NMEA 校验和
 * ========================================================================== */
static void test_checksum(void)
{
    unsigned i;

    printf("\n-- Nmea_ChecksumOk (real data only) --\n");

    for (i = 0; i < sizeof(k_real) / sizeof(k_real[0]); i++)
    {
        char name[96];
        sprintf(name, "real line %u passes checksum", i);
        check(Nmea_ChecksumOk(k_real[i]) == 1, name);
    }

    /* 反例：在真实语句上故意改一位，必须失败 */
    check(Nmea_ChecksumOk("$GNRMC,,V,,,,,,,,,,N*4E") == 0, "wrong checksum rejected");
    check(Nmea_ChecksumOk("GNRMC,,V,,,,,,,,,,N*4D") == 0, "missing $ rejected");
    check(Nmea_ChecksumOk("$GNRMC,,V,,,,,,,,,,N") == 0, "missing * rejected");
    check(Nmea_ChecksumOk("$GNRMC,,V,,,,,,,,,,N*4") == 0, "short checksum rejected");
    check(Nmea_ChecksumOk("$GNRMC,,V,,,,,,,,,,N*ZZ") == 0, "non-hex checksum rejected");
    check(Nmea_ChecksumOk("") == 0, "empty string rejected");
    check(Nmea_ChecksumOk(NULL) == 0, "NULL rejected");
    check(Nmea_ChecksumOk("$*00") == 1, "empty body XOR is 00");
}

/* ============================================================================
 * 二、逗号分段（用真实语句）
 * ========================================================================== */
static void test_split(void)
{
    NmeaFields f;

    printf("\n-- Nmea_Split (real data only) --\n");

    /* 真实的无定位 RMC 有 13 段，中间全是空字段 */
    check(Nmea_Split(REAL_RMC_V, &f) == 1, "split real V RMC");
    check(f.n == 13, "real V RMC has 13 fields");
    check(strcmp(f.f[0], "GNRMC") == 0, "field0 is talker");
    check(strcmp(f.f[1], "") == 0, "field1 (UTC) is empty");
    check(strcmp(f.f[2], "V") == 0, "field2 is status V");
    check(strcmp(f.f[3], "") == 0, "field3 (lat) is empty");
    check(strcmp(f.f[12], "N") == 0, "field12 is N");
    check(f.truncated == 0, "no truncation");

    /* 真实的 GGA：质量 0、卫星 00、HDOP 25.5 */
    check(Nmea_Split(REAL_GGA_0, &f) == 1, "split real GGA");
    check(strcmp(f.f[0], "GNGGA") == 0, "GGA talker");
    check(strcmp(f.f[6], "0") == 0, "GGA quality is 0");
    check(strcmp(f.f[7], "00") == 0, "GGA sats is 00");
    check(strcmp(f.f[8], "25.5") == 0, "GGA hdop is 25.5");

    /* 语句名里没有 '$' 必须失败 */
    check(Nmea_Split("GNRMC,1,2*00", &f) == 0, "reject without $");

    /* '*' 之后的校验位不能进入字段 */
    Nmea_Split("$ABC,1,2*7F", &f);
    check(f.n == 3, "CS excluded from fields");
    check(strcmp(f.f[2], "2") == 0, "last field clean of CS");

    /* 字段过多要置 truncated，且不越界 */
    {
        char big[512];
        int  i, wrote = 0;
        big[wrote++] = '$';
        for (i = 0; i < 40; i++) { big[wrote++] = 'a'; big[wrote++] = ','; }
        big[wrote++] = '*'; big[wrote++] = '0'; big[wrote++] = '0';
        big[wrote] = '\0';

        check(Nmea_Split(big, &f) == 1, "split too-many-fields returns 1");
        check(f.n == NMEA_MAX_FIELDS, "field count clamped to max");
        check(f.truncated == 1, "truncated flag set");
    }
}

/* ============================================================================
 * 三、算法换算：度分 -> 十进制度（纯数学，输入是数值不是 GPS 数据）
 * ========================================================================== */
static void test_algo_ddmm(void)
{
    int32_t v;

    printf("\n-- algo: Rmc_DdmmToE7 --\n");

    check(Rmc_DdmmToE7("3150.70613", &v) == 1 && v == 318451022, "31 50.70613 -> 318451022");
    check(Rmc_DdmmToE7("11711.90887", &v) == 1 && v == 1171984812, "117 11.90887 -> 1171984812");
    check(Rmc_DdmmToE7("5432.10000", &v) == 1 && v == 545350000, "54 32.1 -> 545350000");
    check(Rmc_DdmmToE7("06812.50000", &v) == 1 && v == 682083333, "68 12.5 -> 682083333");
    check(Rmc_DdmmToE7("4000.00000", &v) == 1 && v == 400000000, "40 N -> 400000000");
    check(Rmc_DdmmToE7("11600.00000", &v) == 1 && v == 1160000000, "116 E -> 1160000000");
    check(Rmc_DdmmToE7("0000.00000", &v) == 1 && v == 0, "0,0 -> 0");
    check(Rmc_DdmmToE7("9000.00000", &v) == 1 && v == 900000000, "90 N -> 900000000");

    check(Rmc_DdmmToE7("", &v) == 0, "empty rejected");
    check(Rmc_DdmmToE7("abc", &v) == 0, "alpha rejected");
    check(Rmc_DdmmToE7("12.3x", &v) == 0, "trailing garbage rejected");
    check(Rmc_DdmmToE7(NULL, &v) == 0, "NULL rejected");
}

/* ============================================================================
 * 四、算法换算：坐标格式化（纯数学）
 * ========================================================================== */
static void test_algo_format(void)
{
    char b[40];

    printf("\n-- algo: Fmt_E7 / Fmt_Dms --\n");

    Fmt_E7(b, sizeof(b), 318451022);
    check(strcmp(b, "31.8451022") == 0, "E7 318451022 -> 31.8451022");
    Fmt_E7(b, sizeof(b), -682083333);
    check(strcmp(b, "-68.2083333") == 0, "E7 negative keeps minus");
    Fmt_E7(b, sizeof(b), 0);
    check(strcmp(b, "0.0000000") == 0, "E7 zero");

    /* 31.8451022 度 = 31 度 50 分 42.368 秒 -> 四舍五入 42.4 */
    Fmt_Dms(b, sizeof(b), 318451022, 'N', 'S');
    check(strcmp(b, "31d50'42.4\"N") == 0, "DMS 31d50'42.4\"N");
    /* 117.1984812 度 = 117 度 11 分 54.5 秒 */
    Fmt_Dms(b, sizeof(b), 1171984812, 'E', 'W');
    check(strcmp(b, "117d11'54.5\"E") == 0, "DMS 117d11'54.5\"E");
    /* 54.535 度 = 54 度 32 分 6.0 秒 */
    Fmt_Dms(b, sizeof(b), -545350000, 'N', 'S');
    check(strcmp(b, "54d32'06.0\"S") == 0, "DMS south uses S");
    /* 68.2083333 度 = 68 度 12 分 30.0 秒（四舍五入，不是 29.9）*/
    Fmt_Dms(b, sizeof(b), -682083333, 'E', 'W');
    check(strcmp(b, "68d12'30.0\"W") == 0, "DMS rounds to 30.0, not 29.9");
    Fmt_Dms(b, sizeof(b), 0, 'N', 'S');
    check(strcmp(b, "0d00'00.0\"N") == 0, "DMS zero");
    Fmt_Dms(b, sizeof(b), 900000000, 'N', 'S');
    check(strcmp(b, "90d00'00.0\"N") == 0, "DMS 90 degrees");
}

/* ============================================================================
 * 五、RMC 解析（只用真实语句）
 * ========================================================================== */
static void test_rmc_real(void)
{
    RmcData d;

    printf("\n-- Rmc_Parse (real data only) --\n");

    /* 真实的无定位 RMC：状态 V、字段全空 */
    check(Rmc_Parse(REAL_RMC_V, &d) == 1, "parse real V RMC");
    check(d.valid == 0, "V means invalid");
    check(d.has_pos == 0, "V has no position");
    check(d.lat_e7 == 0 && d.lon_e7 == 0, "V leaves coords at zero");
    check(d.has_time == 0, "V has no time");
    check(d.has_date == 0, "V has no date");
    check(d.speed_kmh_x1000 == 0, "V has no speed");

    /* 非 RMC 语句返回 0，且不能残留上一次的数据 */
    check(Rmc_Parse(REAL_GGA_0, &d) == 0, "GGA is not RMC");
    check(d.valid == 0 && d.has_pos == 0, "non-RMC leaves struct zeroed");
    check(Rmc_Parse(REAL_GSV_0, &d) == 0, "GSV is not RMC");
    check(Rmc_Parse(REAL_TXT_VER, &d) == 0, "TXT is not RMC");
}

/* ============================================================================
 * 六、GGA 解析（只用真实语句）
 * ========================================================================== */
static void test_gga_real(void)
{
    GgaData d;

    printf("\n-- Gga_Parse (real data only) --\n");

    check(Gga_Parse(REAL_GGA_0, &d) == 1, "parse real GGA");
    check(d.has_fix == 0, "quality 0 -> no fix");
    check(d.quality == 0, "quality value 0");
    check(d.sats_used == 0, "sats 00 -> 0");
    check(d.has_hdop == 1 && d.hdop_x10 == 255, "HDOP 25.5 -> 255");

    check(Gga_Parse(REAL_RMC_V, &d) == 0, "RMC is not GGA");
    check(Gga_Parse(REAL_GSV_0, &d) == 0, "GSV is not GGA");
}

/* ============================================================================
 * 七、卫星数与天线告警（只用真实语句）
 * ========================================================================== */
static void test_sats_antenna_real(void)
{
    GpsFix g;

    printf("\n-- sats & antenna (real data only) --\n");

    GpsFix_Init(&g, 3000, 5000);

    /* 真实的 GSV 报 0 颗 */
    GpsFix_OnLine(&g, REAL_GSV_0, 100);
    check(g.has_gsv == 1, "real GSV parsed");
    check(g.sats_visible == 0, "real GSV reports 0 visible");
    check(g.state == GPS_FIX_INVALID, "GSV moves NO_DATA -> FIX_INVALID");
    check(g.not_rmc == 0, "GSV is recognized, not counted as unknown");

    /* 真正不认识的语句（VTG）才算 not_rmc */
    GpsFix_OnLine(&g, "$GNVTG,,,,,,,,,N*2E", 150);
    check(g.not_rmc == 1, "VTG counted as unknown sentence");

    /* 真实的 GGA 报质量 0、卫星 0 */
    GpsFix_OnLine(&g, REAL_GGA_0, 200);
    check(g.has_gga == 1, "real GGA parsed");
    check(g.quality == 0 && g.sats_used == 0, "real GGA: no fix, 0 sats");
    check(g.state == GPS_FIX_INVALID, "GGA alone does not set FIX_VALID");

    /* 真实的天线告警 */
    check(g.antenna_open == 0, "no antenna warning yet");
    check(GpsFix_Antenna(&g, 300) == GPS_ANT_UNKNOWN,
          "antenna UNKNOWN before enough lines");
    GpsFix_OnLine(&g, REAL_TXT_ANT, 400);
    check(g.antenna_open == 1, "ANTENNA OPEN sets flag");
    check(g.antenna_warn_count == 1, "antenna warning counted");
    check(GpsFix_Antenna(&g, 410) == GPS_ANT_OPEN, "antenna OPEN right after warning");

    /* 普通版本 TXT 不能误判成天线告警 */
    {
        GpsFix h;
        GpsFix_Init(&h, 3000, 5000);
        GpsFix_OnLine(&h, REAL_TXT_VER, 100);
        check(h.antenna_open == 0, "version TXT is not an antenna warning");
        check(h.antenna_warn_count == 0, "version TXT not counted");
    }

    /* 告警过期 + 语句不够 -> 只能判"未知"，不能判 OK */
    check(GpsFix_Antenna(&g, 400 + 11000) == GPS_ANT_UNKNOWN,
          "expired warning + few lines -> UNKNOWN, not OK");

    /* 语句够了 + 没告警 -> OK */
    {
        GpsFix h;
        unsigned i;
        GpsFix_Init(&h, 3000, 5000);
        for (i = 0; i < GPS_ANTENNA_CONFIRM_LINES; i++)
        {
            GpsFix_OnLine(&h, REAL_GSV_0, 1000 + i);
        }
        check(h.lines >= GPS_ANTENNA_CONFIRM_LINES, "enough lines collected");
        check(GpsFix_Antenna(&h, 5000) == GPS_ANT_OK, "no warning + enough lines -> OK");
    }

    /* 告警过期 + 语句够 -> 恢复 OK（修好天线后能自动恢复，不用重启）*/
    {
        GpsFix h;
        unsigned i;
        GpsFix_Init(&h, 3000, 5000);
        GpsFix_OnLine(&h, REAL_TXT_ANT, 100);
        for (i = 0; i < GPS_ANTENNA_CONFIRM_LINES; i++)
        {
            GpsFix_OnLine(&h, REAL_GSV_0, 200 + i);
        }
        check(GpsFix_Antenna(&h, 300) == GPS_ANT_OPEN, "fresh warning -> OPEN");
        check(GpsFix_Antenna(&h, 100 + GPS_ANTENNA_WARN_HOLD_MS + 1000) == GPS_ANT_OK,
              "expired warning + enough lines -> back to OK");
    }
}

/* ============================================================================
 * 八、定位子状态机（只用真实语句，因此只覆盖"从未定位"的路径）
 * ========================================================================== */
static void test_state_machine_real(void)
{
    GpsFix g;

    printf("\n-- GpsFix state machine (real data only) --\n");

    GpsFix_Init(&g, 3000, 5000);
    check(g.state == GPS_NO_DATA, "init -> NO_DATA");
    check(g.had_fix == 0, "init: never had a fix");

    GpsFix_OnLine(&g, REAL_GSV_0, 100);
    check(g.state == GPS_FIX_INVALID, "first NMEA line -> FIX_INVALID");
    check(g.lines == 1, "line counted");

    GpsFix_OnLine(&g, REAL_RMC_V, 200);
    check(g.state == GPS_FIX_INVALID, "V RMC without prior fix -> FIX_INVALID");
    check(g.last.valid == 0, "last RMC recorded as invalid");

    /* 坏校验和：不计入、不改状态 */
    {
        GpsFix h;
        GpsFix_Init(&h, 3000, 5000);
        GpsFix_OnLine(&h, "$GNRMC,,V,,,,,,,,,,N*4E", 100);
        check(h.state == GPS_NO_DATA, "bad checksum does not change state");
        check(h.bad_checksum == 1, "bad checksum counted");
        check(h.lines == 0, "bad checksum not counted as line");
    }

    /* 链路超时 -> NO_DATA */
    GpsFix_OnLine(&g, REAL_GSV_0, 1000);
    check(g.state == GPS_FIX_INVALID, "still FIX_INVALID before timeout");
    GpsFix_Tick(&g, 1000 + 5001);
    check(g.state == GPS_NO_DATA, "no line for >5s -> NO_DATA");

    /* 链路恢复 */
    GpsFix_OnLine(&g, REAL_GSV_0, 20000);
    check(g.state == GPS_FIX_INVALID, "back to FIX_INVALID after link recovers");

    /* 定位年龄：从未定位过时是 0xFFFFFFFF */
    check(GpsFix_AgeMs(&g, 20001) == 0xFFFFFFFFu, "age is 0xFFFFFFFF when never fixed");
}

/* ============================================================================
 * 九、用真实抓包跑完整流水
 * ========================================================================== */
static void test_real_capture(void)
{
    FILE   *fp;
    char    line[256];
    GpsFix  g;
    int     n = 0, ok = 0;
    uint32_t t = 0;

    printf("\n-- real capture end-to-end --\n");

    fp = fopen("testdata/nmea_no_fix.txt", "r");
    if (fp == NULL) { fp = fopen("../testdata/nmea_no_fix.txt", "r"); }
    if (fp == NULL)
    {
        printf("  SKIP  cannot open testdata/nmea_no_fix.txt\n");
        return;
    }

    GpsFix_Init(&g, 3000, 5000);

    while (fgets(line, sizeof(line), fp) != NULL)
    {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
        {
            line[--len] = '\0';
        }
        if (len == 0 || line[0] != '$') { continue; }

        n++;
        if (Nmea_ChecksumOk(line)) { ok++; }
        t += 90;
        GpsFix_OnLine(&g, line, t);
        GpsFix_Tick(&g, t);
    }
    fclose(fp);

    printf("  (read %d sentences, %d passed checksum)\n", n, ok);

    check(n > 0, "capture file has lines");
    check(ok == n, "every captured line passes checksum");
    check(g.bad_checksum == 0, "no checksum failures in capture");
    check(g.state == GPS_FIX_INVALID, "capture ends in FIX_INVALID");
    check(g.had_fix == 0, "capture never had a fix");
    check(g.sats_visible == 0, "capture reports 0 visible satellites");
    check(g.lines == (uint32_t)n, "all lines fed to state machine");
}

int main(void)
{
    printf("== gps parse tests (real data only) ==\n");

    test_checksum();
    test_split();
    test_algo_ddmm();
    test_algo_format();
    test_rmc_real();
    test_gga_real();
    test_sats_antenna_real();
    test_state_machine_real();
    test_real_capture();

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    printf("NOTE: the valid-fix path (status A with coordinates) is NOT covered --\n");
    printf("      no real capture with a fix exists yet. Add one to testdata/ later.\n");
    return g_fail == 0 ? 0 : 1;
}
