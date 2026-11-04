/**
 * 离屏渲染工具（只用于开发验证，不参与游戏运行）
 *
 * 它复用和预览器完全相同的 game.c / app.c / render.c / pc_bsp_lcd.c，
 * 但不开窗口，直接把几帧画面导出成 BMP，方便用看图工具检查布局。
 *
 * 用法：  make_frame.exe ready.bmp running.bmp win.bmp
 */
#include <stdio.h>
#include <string.h>

#include "pc_bsp_lcd.h"
#include "../Game_LCD/User/game/render.h"

/* 写 24 位 BMP（BGR，行序自下而上）*/
static int save_bmp(const char *path)
{
    FILE *fp = fopen(path, "wb");
    int x, y;
    unsigned char hdr[54];
    unsigned char row[800 * 3];
    int row_size = 800 * 3;
    unsigned char pad[4] = { 0, 0, 0, 0 };
    int pad_size = (4 - (row_size % 4)) % 4;
    unsigned int file_size;

    if (fp == NULL) { return -1; }

    file_size = 54u + (unsigned int)(row_size + pad_size) * 480u;

    memset(hdr, 0, sizeof(hdr));
    hdr[0] = 'B'; hdr[1] = 'M';
    hdr[2]  = (unsigned char)(file_size);
    hdr[3]  = (unsigned char)(file_size >> 8);
    hdr[4]  = (unsigned char)(file_size >> 16);
    hdr[5]  = (unsigned char)(file_size >> 24);
    hdr[10] = 54;
    hdr[14] = 40;
    hdr[18] = (unsigned char)(800);
    hdr[19] = (unsigned char)(800 >> 8);
    hdr[20] = (unsigned char)(800 >> 16);
    hdr[21] = (unsigned char)(800 >> 24);
    hdr[22] = (unsigned char)(480);
    hdr[23] = (unsigned char)(480 >> 8);
    hdr[24] = (unsigned char)(480 >> 16);
    hdr[25] = (unsigned char)(480 >> 24);
    hdr[26] = 1;                        /* planes */
    hdr[28] = 24;                       /* bpp */
    hdr[34] = (unsigned char)((row_size + pad_size) * 480);
    hdr[35] = (unsigned char)(((row_size + pad_size) * 480) >> 8);
    hdr[36] = (unsigned char)(((row_size + pad_size) * 480) >> 16);
    hdr[37] = (unsigned char)(((row_size + pad_size) * 480) >> 24);
    fwrite(hdr, 1, 54, fp);

    for (y = 479; y >= 0; y--)              /* BMP 自下而上 */
    {
        for (x = 0; x < 800; x++)
        {
            unsigned short c = g_pc_fb[y * 800 + x];
            unsigned int r = (c >> 11) & 0x1F;
            unsigned int g = (c >> 5) & 0x3F;
            unsigned int b = c & 0x1F;
            r = (r << 3) | (r >> 2);
            g = (g << 2) | (g >> 4);
            b = (b << 3) | (b >> 2);
            row[x * 3 + 0] = (unsigned char)b;
            row[x * 3 + 1] = (unsigned char)g;
            row[x * 3 + 2] = (unsigned char)r;
        }
        fwrite(row, 1, row_size, fp);
        if (pad_size) { fwrite(pad, 1, pad_size, fp); }
    }

    fclose(fp);
    return 0;
}

int main(int argc, char **argv)
{
    static App_Ctx app;
    int i;

    if (argc < 2)
    {
        printf("usage: make_frame out1.bmp [out2.bmp ...]\n");
        return 1;
    }

    /* ---- 帧 1：READY ---- */
    App_Init(&app);
    Render_Begin(&app);
    save_bmp(argv[1]);
    printf("saved %s  (READY, level %u)\n", argv[1],
           (unsigned)(app.game.level_index + 1));

    /* ---- 帧 2：RUNNING，球移到目标附近 ---- */
    if (argc >= 3)
    {
        float tilt = 0.0f;
        int k;

        App_DoAction(&app, ACT_START);
        Render_Begin(&app);

        /* 真的用物理推它走一段，而不是硬塞坐标 */
        for (k = 0; k < 120; k++)
        {
            App_Update(&app, -3000.0f, 0.0f, 0.033f);   /* 往右推 */
        }
        for (k = 0; k < 90; k++)
        {
            App_Update(&app, 0.0f, -3000.0f, 0.033f);   /* 往上推 */
        }
        tilt = tilt; (void)tilt;

        Render_Begin(&app);
        save_bmp(argv[2]);
        printf("saved %s  (RUNNING, ball at %.0f,%.0f)\n", argv[2],
               app.game.ball.x, app.game.ball.y);
    }

    /* ---- 帧 3：WIN 横幅 ---- */
    if (argc >= 4)
    {
        const Game_Level *L = Game_GetLevel(&app.game);

        /* 把球放到目标区中心，跑一帧让它判定 WIN */
        app.game.ball.x = (float)(L->target.x + L->target.w / 2);
        app.game.ball.y = (float)(L->target.y + L->target.h / 2);
        App_Update(&app, 0.0f, 0.0f, 0.033f);
        Render_Begin(&app);
        save_bmp(argv[3]);
        printf("saved %s  (state=%s)\n", argv[3], App_StateName(&app));
    }

    /* ---- 帧 4：FAIL 横幅 ---- */
    if (argc >= 5)
    {
        int guard = 0;
        const Game_Level *L;

        /* 直接用 Game_NextLevel 跳到第一个带红区的关卡（第 7 关）。
         * 不能靠 ACT_NEXT_LEVEL —— 那个动作只在 WIN 状态有效。 */
        App_Init(&app);
        while (!Game_GetLevel(&app.game)->hazard_on && guard++ < GAME_LEVEL_COUNT)
        {
            Game_NextLevel(&app.game);
        }
        App_Relayout(&app);
        App_DoAction(&app, ACT_START);          /* READY -> RUNNING */
        Render_Begin(&app);

        L = Game_GetLevel(&app.game);
        app.game.ball.x = (float)(L->hazard.x + L->hazard.w / 2);
        app.game.ball.y = (float)(L->hazard.y + L->hazard.h / 2);
        App_Update(&app, 0.0f, 0.0f, 0.033f);
        Render_Begin(&app);
        save_bmp(argv[4]);
        printf("saved %s  (state=%s, level %u)\n", argv[4], App_StateName(&app),
               (unsigned)(app.game.level_index + 1));
    }

    (void)i;
    return 0;
}
