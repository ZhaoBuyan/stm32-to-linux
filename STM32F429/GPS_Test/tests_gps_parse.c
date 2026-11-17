/**
 ******************************************************************************
 * @file    tests_gps_parse.c
 * @brief   nmea.c / rmc.c / gps_fix.c 的 PC 端单元测试
 *
 * 编译运行：
 *   gcc -std=c11 -Wall -Wextra -IUser/gps tests_gps_parse.c \
 *       User/gps/nmea.c User/gps/rmc.c User/gps/gps_fix.c -o tests_gps_parse.exe
 *   ./tests_gps_parse.exe
 *
 * 测试数据分两类：
 *   ① **真实抓包** testdata/nmea_no_fix.txt —— 2026-11-16 室内、天线开路时抓的
 *   ② **构造样本** —— 手上没有"定位成功"的真实数据，只能手工造，
 *      校验和用脚本算准。文件里标注了哪些是构造的，不冒充实测。
 ******************************************************************************
 */
#include <stdio.h>
#include <string.h>

#include "gps_fix.h"
#include "fmt.h"
#include "gga.h"
#include "nmea.h"
#include "rmc.h"

static int g_pass = 0;
static int g_fail = 0;

static void check(int cond, const char *name)
{
    if (cond) { g_pass++; printf("  PASS  %s\n", name); }
    else      { g_fail++; printf("  FAIL  %s  <--\n", name); }
}

/* ---------------- 真实抓包（校验和由脚本算过：24/24 通过）---------------- */
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
    "$GPTXT,01,01,02,HW=ATGM332D,0005063714758*12",
    "$GPTXT,01,01,02,IC=AT6558-5N-31-0C510800,FE28CKJ-F1-025348*52",
};

/* ---------------- 构造样本（校验和已算准，标注为"构造"）----------------
 * 有效定位：31°50.70613'N 117°11.90887'E，12.345 节，UTC 08:18:36，2026-02-06
 */
static const char *k_valid_rmc =
    "$GNRMC,081836.00,A,3150.70613,N,11711.90887,E,12.345,84.40,060226,,,A*70";
/* 南半球 + 西经：54°32.10000'S 068°12.50000'W */
static const char *k_south_west_rmc =
    "$GNRMC,120000.00,A,5432.10000,S,06812.50000,W,0.000,0.00,010126,,,A*71";
/* 赤道 / 本初子午线 */
static const char *k_zero_rmc =
    "$GNRMC,000000.00,A,0000.00000,N,00000.00000,E,0.000,0.00,010126,,,A*74";
/* 状态 A 但经纬度为空（异常但可能出现的输入）*/
static const char *k_a_but_empty =
    "$GNRMC,235959.99,A,,,,,,,,,,N*75";
/* 高速 30 节 -> 55.56 km/h */
static const char *k_fast_rmc =
    "$GNRMC,090000.00,A,4000.00000,N,11600.00000,E,30.000,90.00,010126,,,A*75";
/* 字段不足 */
static const char *k_short_rmc = "$GNRMC,,A*14";

/* ============================================================================
 * 一、NMEA 校验和
 * ========================================================================== */
static void test_checksum(void)
{
    unsigned i;

    printf("\n-- Nmea_ChecksumOk --\n");

    for (i = 0; i < sizeof(k_real) / sizeof(k_real[0]); i++)
    {
        char name[96];
        sprintf(name, "real line %u passes checksum", i);
        check(Nmea_ChecksumOk(k_real[i]) == 1, name);
    }

    check(Nmea_ChecksumOk(k_valid_rmc) == 1, "constructed valid RMC passes");
    check(Nmea_ChecksumOk(k_south_west_rmc) == 1, "constructed S/W RMC passes");
    check(Nmea_ChecksumOk(k_zero_rmc) == 1, "constructed zero RMC passes");
    check(Nmea_ChecksumOk(k_a_but_empty) == 1, "constructed A-but-empty passes");
    check(Nmea_ChecksumOk(k_fast_rmc) == 1, "constructed fast RMC passes");

    /* 反例：把校验位改一位，必须失败 */
    check(Nmea_ChecksumOk("$GNRMC,,V,,,,,,,,,,N*4E") == 0, "wrong checksum rejected");
    /* 缺 '$' */
    check(Nmea_ChecksumOk("GNRMC,,V,,,,,,,,,,N*4D") == 0, "missing $ rejected");
    /* 缺 '*' */
    check(Nmea_ChecksumOk("$GNRMC,,V,,,,,,,,,,N") == 0, "missing * rejected");
    /* '*' 后面不够两位 */
    check(Nmea_ChecksumOk("$GNRMC,,V,,,,,,,,,,N*4") == 0, "short checksum rejected");
    /* 非十六进制 */
    check(Nmea_ChecksumOk("$GNRMC,,V,,,,,,,,,,N*ZZ") == 0, "non-hex checksum rejected");
    /* 空串 / NULL */
    check(Nmea_ChecksumOk("") == 0, "empty string rejected");
    check(Nmea_ChecksumOk(NULL) == 0, "NULL rejected");
    /* 只有 '$' 和 '*' */
    check(Nmea_ChecksumOk("$*00") == 1, "empty body XOR is 00");
}

/* ============================================================================
 * 二、逗号分段
 * ========================================================================== */
static void test_split(void)
{
    NmeaFields f;

    printf("\n-- Nmea_Split --\n");

    /* 构造的有效 RMC 应当有 13 段 */
    check(Nmea_Split(k_valid_rmc, &f) == 1, "split valid RMC");
    check(f.n == 13, "valid RMC has 13 fields");
    check(strcmp(f.f[0], "GNRMC") == 0, "field0 is talker");
    check(strcmp(f.f[1], "081836.00") == 0, "field1 is UTC");
    check(strcmp(f.f[2], "A") == 0, "field2 is status A");
    check(strcmp(f.f[3], "3150.70613") == 0, "field3 is latitude");
    check(strcmp(f.f[7], "12.345") == 0, "field7 is speed");
    check(strcmp(f.f[9], "060226") == 0, "field9 is date");
    check(f.truncated == 0, "no truncation");

    /* 空字段必须保留成空串，不能被吞掉（否则字段错位）*/
    check(Nmea_Split("$GNRMC,,V,,,,,,,,,,N*4D", &f) == 1, "split V RMC");
    check(f.n == 13, "V RMC still has 13 fields (empties kept)");
    check(strcmp(f.f[1], "") == 0, "empty UTC stays empty");
    check(strcmp(f.f[2], "V") == 0, "status V at correct index");
    check(strcmp(f.f[12], "N") == 0, "field12 is N");

    /* 语句名里没有 '$' 时必须失败 */
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
        for (i = 0; i < 40; i++)
        {
            big[wrote++] = 'a';
            big[wrote++] = ',';
        }
        big[wrote++] = '*';
        big[wrote++] = '0';
        big[wrote++] = '0';
        big[wrote] = '\0';

        check(Nmea_Split(big, &f) == 1, "split too-many-fields returns 1");
        check(f.n == NMEA_MAX_FIELDS, "field count clamped to max");
        check(f.truncated == 1, "truncated flag set");
    }
}

/* ============================================================================
 * 三、度分 -> 十进制度
 * ========================================================================== */
static void test_ddmm(void)
{
    int32_t v;

    printf("\n-- Rmc_DdmmToE7 --\n");

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
 * 四、RMC 解析
 * ========================================================================== */
static void test_rmc(void)
{
    RmcData d;

    printf("\n-- Rmc_Parse --\n");

    /* 有效定位（构造样本）*/
    check(Rmc_Parse(k_valid_rmc, &d) == 1, "parse valid RMC");
    check(d.valid == 1, "valid flag set");
    check(d.has_pos == 1, "has_pos set");
    check(d.lat_e7 == 318451022, "lat_e7 correct");
    check(d.lon_e7 == 1171984812, "lon_e7 correct");
    check(d.speed_kmh_x1000 == 22863, "12.345 knots -> 22.863 km/h");
    check(d.has_time == 1 && d.utc_ms_of_day == 29916000u, "UTC 08:18:36 -> 29916000 ms");
    check(d.has_date == 1 && d.year == 2026 && d.month == 2 && d.day == 6,
          "date 060226 -> 2026-02-06");

    /* 南半球西经：符号必须为负 */
    check(Rmc_Parse(k_south_west_rmc, &d) == 1, "parse S/W RMC");
    check(d.lat_e7 == -545350000, "south latitude negative");
    check(d.lon_e7 == -682083333, "west longitude negative");
    check(d.speed_kmh_x1000 == 0, "0 knots -> 0 km/h");

    /* 赤道本初子午线 */
    check(Rmc_Parse(k_zero_rmc, &d) == 1, "parse zero RMC");
    check(d.lat_e7 == 0 && d.lon_e7 == 0, "zero coordinates");

    /* 高速 */
    check(Rmc_Parse(k_fast_rmc, &d) == 1, "parse fast RMC");
    check(d.speed_kmh_x1000 == 55560, "30 knots -> 55.56 km/h");

    /* 真实抓包：状态 V，经纬度空 —— **最关键的一条** */
    check(Rmc_Parse("$GNRMC,,V,,,,,,,,,,N*4D", &d) == 1, "parse real V RMC");
    check(d.valid == 0, "V means invalid");
    check(d.has_pos == 0, "V has no position");
    check(d.lat_e7 == 0 && d.lon_e7 == 0, "V leaves coords at zero");
    check(d.has_time == 0, "V has no time");
    check(d.has_date == 0, "V has no date");

    /* 状态 A 但经纬度空：valid=1 但 has_pos=0 —— 不能当有定位 */
    check(Rmc_Parse(k_a_but_empty, &d) == 1, "parse A-but-empty RMC");
    check(d.valid == 1, "A-but-empty: valid flag is 1");
    check(d.has_pos == 0, "A-but-empty: has_pos is 0");
    check(d.has_time == 1 && d.utc_ms_of_day == 86399990u, "23:59:59.99 -> 86399990 ms");

    /* 字段不足：返回 -1 */
    check(Rmc_Parse(k_short_rmc, &d) == -1, "3-field RMC is truncated -> -1");

    /* 不是 RMC：返回 0 */
    check(Rmc_Parse("$GNGGA,,,,,,0,00,25.5,,,,,,*64", &d) == 0, "GGA is not RMC");
    check(Rmc_Parse("$GPGSV,1,1,00*79", &d) == 0, "GSV is not RMC");
    check(Rmc_Parse("$GPTXT,01,01,02,HW=ATGM332D,0005063714758*12", &d) == 0,
          "TXT is not RMC");

    /* 解析失败时不能残留上一次的数据 */
    check(Rmc_Parse("$GNGGA,,,,,,0,00,25.5,,,,,,*64", &d) == 0, "non-RMC returns 0");
    check(d.valid == 0 && d.has_pos == 0, "non-RMC leaves struct zeroed");
}

/* ============================================================================
 * 五、定位子状态机
 * ========================================================================== */
static const char *k_gsv = "$GPGSV,1,1,00*79";
static const char *k_rmc_v = "$GNRMC,,V,,,,,,,,,,N*4D";

static void test_state_machine(void)
{
    GpsFix g;

    printf("\n-- GpsFix state machine --\n");

    /* 1. 刚初始化：NO_DATA */
    GpsFix_Init(&g, 3000, 5000);
    check(g.state == GPS_NO_DATA, "init -> NO_DATA");

    /* 2. 收到一条非 RMC 语句（链路通了但还没 RMC）：FIX_INVALID
     *    注意 GSV 现在有自己的分支，会被解析成"可见卫星数"，
     *    不再是"不认识的语句"，所以 not_rmc 不该增加。 */
    GpsFix_OnLine(&g, k_gsv, 100);
    check(g.state == GPS_FIX_INVALID, "first NMEA line -> FIX_INVALID");
    check(g.lines == 1, "line counted");
    check(g.has_gsv == 1, "GSV recognized and parsed");
    check(g.not_rmc == 0, "GSV is recognized, not counted as unknown");

    /* 2b. 真正不认识的语句（VTG）才算 not_rmc */
    GpsFix_OnLine(&g, "$GNVTG,,,,,,,,,N*2E", 150);
    check(g.not_rmc == 1, "VTG counted as unknown sentence");
    check(g.state == GPS_FIX_INVALID, "VTG does not change state");

    /* 3. 收到无定位的 RMC：仍 FIX_INVALID（从未定位成功过）*/
    GpsFix_OnLine(&g, k_rmc_v, 200);
    check(g.state == GPS_FIX_INVALID, "V RMC without prior fix -> FIX_INVALID");
    check(g.last.valid == 0, "last RMC recorded as invalid");

    /* 4. 收到有效定位：FIX_VALID */
    GpsFix_OnLine(&g, k_valid_rmc, 1000);
    check(g.state == GPS_FIX_VALID, "valid RMC -> FIX_VALID");
    check(g.had_fix == 1, "had_fix set");
    check(g.fix.lat_e7 == 318451022, "fix lat stored");
    check(GpsFix_AgeMs(&g, 1000) == 0, "fix age 0 right after");

    /* 5. 新鲜期内 Tick 不应变状态 */
    GpsFix_Tick(&g, 2000);
    check(g.state == GPS_FIX_VALID, "still VALID at 1s");

    /* 6. 超过保鲜期（3s）Tick：STALE */
    GpsFix_Tick(&g, 4001);
    check(g.state == GPS_STALE, "no new fix for >3s -> STALE");
    check(GpsFix_AgeMs(&g, 4001) == 3001, "fix age computed");
    /* STALE 时必须还能拿到最后一次坐标（界面要显示"过期"位置）*/
    check(g.fix.lat_e7 == 318451022, "last fix still available in STALE");

    /* 7. STALE 后收到有效的 V 语句：仍 STALE（不退回 FIX_INVALID）*/
    GpsFix_OnLine(&g, k_rmc_v, 4200);
    check(g.state == GPS_STALE, "V after having a fix -> STALE, not FIX_INVALID");

    /* 8. 再次收到有效定位：回到 FIX_VALID，保鲜期重新计 */
    GpsFix_OnLine(&g, k_valid_rmc, 5000);
    check(g.state == GPS_FIX_VALID, "valid again -> FIX_VALID");
    GpsFix_Tick(&g, 7500);
    check(g.state == GPS_FIX_VALID, "fresh again after re-fix");

    /* 9. 链路超时（5s 没有任何语句）：NO_DATA */
    GpsFix_Tick(&g, 5000 + 5001);
    check(g.state == GPS_NO_DATA, "no line for >5s -> NO_DATA");

    /* 10. 链路恢复后收到有效定位：FIX_VALID */
    GpsFix_OnLine(&g, k_valid_rmc, 20000);
    check(g.state == GPS_FIX_VALID, "recover from NO_DATA -> FIX_VALID");

    /* 11. 坏校验和的语句：不计入，也不改状态 */
    {
        GpsFix h;
        GpsFix_Init(&h, 3000, 5000);
        GpsFix_OnLine(&h, "$GNRMC,,V,,,,,,,,,,N*4E", 100);   /* 故意错 1 位 */
        check(h.state == GPS_NO_DATA, "bad checksum does not change state");
        check(h.bad_checksum == 1, "bad checksum counted");
        check(h.lines == 0, "bad checksum not counted as line");
    }

    /* 12. 毫秒计数回绕：用接近 0xFFFFFFFF 的时刻验证不出现假超时 */
    {
        GpsFix w;
        GpsFix_Init(&w, 3000, 5000);
        GpsFix_OnLine(&w, k_valid_rmc, 0xFFFFFF00u);
        check(w.state == GPS_FIX_VALID, "valid near wrap point");
        GpsFix_Tick(&w, 0x00000200u);          /* 回绕到 0x200，实际过了 0x300 = 768ms */
        check(w.state == GPS_FIX_VALID, "no false stale across wraparound");
        GpsFix_Tick(&w, 0x00001000u);          /* 又过了约 3.5s */
        check(w.state == GPS_STALE, "stale detected after wraparound");
    }
}

/* ============================================================================
 * 六、GGA：卫星数 / 定位质量 / HDOP / 海拔
 * ========================================================================== */
/* 构造：质量1、9颗星、HDOP 0.9、海拔 45.6 m */
static const char *k_gga_fix =
    "$GNGGA,081836.00,3150.70613,N,11711.90887,E,1,09,0.9,45.6,M,10.2,M,,*4F";
/* 真实抓包：无定位、0 颗星、HDOP 25.5 */
static const char *k_gga_nofix = "$GNGGA,,,,,,0,00,25.5,,,,,,*64";
/* 构造：GPS 单频、质量0、HDOP 99.9 */
static const char *k_gga_gp_nofix =
    "$GPGGA,120000.00,5432.10000,S,06812.50000,W,0,00,99.9,,,,,,*61";

static void test_gga(void)
{
    GgaData d;

    printf("\n-- Gga_Parse --\n");

    check(Gga_Parse(k_gga_fix, &d) == 1, "parse GGA with fix");
    check(d.has_fix == 1, "quality 1 -> has_fix");
    check(d.quality == 1, "quality value 1");
    check(d.sats_used == 9, "sats_used 09 -> 9");
    check(d.has_hdop == 1 && d.hdop_x10 == 9, "HDOP 0.9 -> 9 (x10)");
    check(d.has_alt == 1 && d.alt_m == 46, "alt 45.6 -> 46 (rounded)");

    /* 真实抓包：无定位 */
    check(Gga_Parse(k_gga_nofix, &d) == 1, "parse real no-fix GGA");
    check(d.has_fix == 0, "quality 0 -> no fix");
    check(d.quality == 0, "quality value 0");
    check(d.sats_used == 0, "sats_used 00 -> 0");
    check(d.has_hdop == 1 && d.hdop_x10 == 255, "HDOP 25.5 -> 255");

    /* GP 单频、极差 HDOP */
    check(Gga_Parse(k_gga_gp_nofix, &d) == 1, "parse GP GGA no fix");
    check(d.has_fix == 0 && d.sats_used == 0, "GP GGA no fix, 0 sats");
    check(d.hdop_x10 == 999, "HDOP 99.9 -> 999");

    /* 不是 GGA */
    check(Gga_Parse("$GNRMC,,V,,,,,,,,,,N*4D", &d) == 0, "RMC is not GGA");
    check(Gga_Parse("$GPGSV,1,1,00*79", &d) == 0, "GSV is not GGA");
    /* 字段不足 */
    check(Gga_Parse("$GNGGA,1,2*00", &d) == -1, "short GGA -> -1");
}

/* ============================================================================
 * 七、GSV 可见卫星数 + TXT 天线告警
 * ========================================================================== */
static const char *k_gsv_8 = 
    "$GPGSV,2,1,08,01,40,083,46,02,17,308,41,12,07,344,39,14,22,228,45*75";
static const char *k_gsv_8b =
    "$GPGSV,2,2,08,24,15,010,33,25,45,120,40,29,60,200,35,31,10,300,20*78";
static const char *k_txt_antenna = "$GPTXT,01,01,01,ANTENNA OPEN*25";
static const char *k_txt_version =
    "$GPTXT,01,01,02,HW=ATGM332D,0005063714758*12";

static void test_sats_and_antenna(void)
{
    GpsFix g;

    printf("\n-- sats & antenna --\n");

    GpsFix_Init(&g, 3000, 5000);

    /* 初始：没有 GSV，可见 0 颗 */
    check(g.sats_visible == 0 && g.has_gsv == 0, "init: no GSV data");

    /* 收到 GSV：可见 8 颗 */
    GpsFix_OnLine(&g, k_gsv_8, 100);
    check(g.has_gsv == 1, "GSV parsed");
    check(g.sats_visible == 8, "GSV total 08 -> 8 visible");
    check(g.state == GPS_FIX_INVALID, "GSV keeps state FIX_INVALID");

    /* 同轮第二条 GSV：总数还是 8，不应累加 */
    GpsFix_OnLine(&g, k_gsv_8b, 200);
    check(g.sats_visible == 8, "second GSV message keeps total 8 (no accumulation)");

    /* 收到 GGA：参与定位 9 颗、质量 1 */
    GpsFix_OnLine(&g, k_gga_fix, 300);
    check(g.has_gga == 1, "GGA parsed");
    check(g.quality == 1, "quality recorded");
    check(g.sats_used == 9, "sats_used recorded");
    check(g.hdop_x10 == 9, "hdop recorded");
    check(g.has_alt == 1 && g.alt_m == 46, "altitude recorded");
    /* GGA 不应该把状态机推到 VALID —— 定位只认 RMC */
    check(g.state == GPS_FIX_INVALID, "GGA alone does not set FIX_VALID");

    /* 天线告警 */
    check(g.antenna_open == 0, "no antenna warning yet");
    check(GpsFix_Antenna(&g, 300) == GPS_ANT_UNKNOWN, "antenna UNKNOWN before enough lines");
    GpsFix_OnLine(&g, k_txt_antenna, 400);
    check(g.antenna_open == 1, "ANTENNA OPEN sets flag");
    check(g.antenna_warn_count == 1, "antenna warning counted");
    check(GpsFix_Antenna(&g, 410) == GPS_ANT_OPEN, "antenna OPEN right after warning");
    /* 告警有效期 10 秒：超过之后应恢复（否则修好天线也永远 FAIL）*/
    /* 告警过期了，但这时才收了 4 条语句 —— 不到阈值，只能判"未知"，
     * 不能因为"没再告警"就说 OK（这正是第一版那个误导的根源）*/
    check(GpsFix_Antenna(&g, 400 + 11000) == GPS_ANT_UNKNOWN,
          "expired warning + few lines -> UNKNOWN, not OK");

    /* 再喂够 30 条语句，此时才有资格判 OK */
    {
        GpsFix h;
        unsigned i;
        GpsFix_Init(&h, 3000, 5000);
        for (i = 0; i < GPS_ANTENNA_CONFIRM_LINES; i++)
        {
            GpsFix_OnLine(&h, k_gsv_8, 1000 + i);
        }
        check(h.lines >= GPS_ANTENNA_CONFIRM_LINES, "enough lines collected");
        check(GpsFix_Antenna(&h, 5000) == GPS_ANT_OK,
              "no warning + enough lines -> OK");
    }

    /* 告警过期 + 语句够多 -> 恢复 OK（修好天线后能自动恢复，不用重启）*/
    {
        GpsFix h;
        unsigned i;
        GpsFix_Init(&h, 3000, 5000);
        GpsFix_OnLine(&h, k_txt_antenna, 100);
        for (i = 0; i < GPS_ANTENNA_CONFIRM_LINES; i++)
        {
            GpsFix_OnLine(&h, k_gsv_8, 200 + i);
        }
        check(GpsFix_Antenna(&h, 300) == GPS_ANT_OPEN, "warning still fresh -> OPEN");
        check(GpsFix_Antenna(&h, 100 + GPS_ANTENNA_WARN_HOLD_MS + 1000) == GPS_ANT_OK,
              "warning expired + enough lines -> back to OK");
    }

    /* 普通版本 TXT 不能误判成天线告警 */
    {
        GpsFix h;
        GpsFix_Init(&h, 3000, 5000);
        GpsFix_OnLine(&h, k_txt_version, 100);
        check(h.antenna_open == 0, "version TXT is not an antenna warning");
        check(h.antenna_warn_count == 0, "version TXT not counted as warning");
    }

    /* RMC 才决定 FIX_VALID */
    GpsFix_OnLine(&g, k_valid_rmc, 500);
    check(g.state == GPS_FIX_VALID, "RMC valid -> FIX_VALID");
    /* 卫星数据在定位后依然保留 */
    check(g.sats_visible == 8 && g.sats_used == 9, "sat counts survive state change");
}

/* ============================================================================
 * 八、用真实抓包跑一遍完整流水（校验 -> 分段 -> 解析 -> 状态机）
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
        printf("  SKIP  找不到 testdata/nmea_no_fix.txt\n");
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
        t += 90;                        /* 每条约 90 ms，模拟 1 Hz 一轮 */
        GpsFix_OnLine(&g, line, t);
        GpsFix_Tick(&g, t);
    }
    fclose(fp);

    printf("  （读到 %d 条语句，校验通过 %d 条）\n", n, ok);

    check(n > 0, "capture file has lines");
    check(ok == n, "every captured line passes checksum");
    check(g.bad_checksum == 0, "no checksum failures in capture");
    check(g.state == GPS_FIX_INVALID, "capture ends in FIX_INVALID (no fix, never had one)");
    check(g.had_fix == 0, "capture never had a fix");
    check(g.lines == (uint32_t)n, "all lines fed to state machine");
}


/* ============================================================================
 * 九、坐标格式化（度分秒最容易算错，单独测）
 * ========================================================================== */
static void test_format(void)
{
    char b[40];

    printf("\n-- Fmt_E7 / Fmt_Dms --\n");

    Fmt_E7(b, sizeof(b), 318451022);
    check(strcmp(b, "31.8451022") == 0, "E7 north  -> 31.8451022");
    Fmt_E7(b, sizeof(b), -682083333);
    check(strcmp(b, "-68.2083333") == 0, "E7 south/west keeps minus");
    Fmt_E7(b, sizeof(b), 0);
    check(strcmp(b, "0.0000000") == 0, "E7 zero");

    /* 31.8451022 度 = 31 度 50 分 42.368 秒 -> 四舍五入 42.4 */
    Fmt_Dms(b, sizeof(b), 318451022, 'N', 'S');
    check(strcmp(b, "31d50'42.4\"N") == 0, "DMS north 31d50'42.4\"N");

    /* 117.1984812 度 = 117 度 11 分 54.5 秒 */
    Fmt_Dms(b, sizeof(b), 1171984812, 'E', 'W');
    check(strcmp(b, "117d11'54.5\"E") == 0, "DMS east 117d11'54.5\"E");

    Fmt_Dms(b, sizeof(b), -545350000, 'N', 'S');
    check(strcmp(b, "54d32'06.0\"S") == 0, "DMS south uses S");

    Fmt_Dms(b, sizeof(b), 0, 'N', 'S');
    check(strcmp(b, "0d00'00.0\"N") == 0, "DMS zero");
    Fmt_Dms(b, sizeof(b), -682083333, 'E', 'W');
    check(strcmp(b, "68d12'30.0\"W") == 0, "DMS west uses W");

    /* 90 度边界 */
    Fmt_Dms(b, sizeof(b), 900000000, 'N', 'S');
    check(strcmp(b, "90d00'00.0\"N") == 0, "DMS 90 degrees");
}
int main(void)
{
    printf("== gps parse tests ==\n");

    test_checksum();
    test_split();
    test_ddmm();
    test_rmc();
    test_gga();
    test_sats_and_antenna();
    test_state_machine();
    test_format();
    test_real_capture();

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
