/**
 ******************************************************************************
 * @file    main_pc.c
 * @brief   姿态感知游戏的 PC 预览器（Windows / 纯 C + Win32 GDI）
 *
 * 它做的事：
 *   1. 开一个客户区恰好 800x480 的窗口
 *   2. 用键盘模拟开发板倾斜（WASD），把倾斜量喂给游戏
 *   3. 调 render.c 把画面写进假显存 g_pc_fb
 *   4. 把 RGB565 转成 32 位，用 StretchDIBits 贴到窗口
 *   5. 把 render.c 收集到的文字用 GDI TextOutA 画上去
 *
 * 游戏逻辑（game.c / app.c / render.c）一行都不用改。
 *
 * 编译：见同目录 build.ps1
 ******************************************************************************
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "pc_bsp_lcd.h"
#include "../Game_LCD/User/game/render.h"

/* ============================================================================
 * 常量
 * ========================================================================== */

#define WIN_W       LCD_PIXEL_WIDTH
#define WIN_H       LCD_PIXEL_HEIGHT
#define FRAME_MS    33                  /* 30 fps，和固件 33ms 周期对齐 */

/* 倾斜模拟：按住逐渐加大，松开回零 */
#define TILT_MAX    6300.0f             /* 实测：满倾时 ax 的偏移量 */
#define TILT_RAMP   18000.0f            /* 每秒变化量 */
#define TILT_DECAY  24000.0f            /* 松开后回零速度 */

/* ============================================================================
 * 全局
 * ========================================================================== */

static uint32_t  s_bgra[WIN_W * WIN_H];         /* GDI 要的 32bpp 缓冲 */
static BITMAPINFO s_bmi;
static HFONT      s_font = NULL;

static float      s_tx = 0.0f;                  /* 当前模拟倾角 */
static float      s_ty = 0.0f;

static int        s_k1_last = 0;                /* 按键边沿检测 */
static int        s_k2_last = 0;

static int        s_running = 1;
static HWND       s_hwnd = NULL;

static LARGE_INTEGER s_freq;
static LARGE_INTEGER s_last;

/* ============================================================================
 * 时间
 * ========================================================================== */

static void timer_init(void)
{
    QueryPerformanceFrequency(&s_freq);
    QueryPerformanceCounter(&s_last);
}

/* 返回距上一帧的秒数（夹在 0.001~0.1 之间，防止调试停住后一帧跳太远）*/
static float timer_dt(void)
{
    LARGE_INTEGER now;
    float dt;

    QueryPerformanceCounter(&now);
    dt = (float)(now.QuadPart - s_last.QuadPart) / (float)s_freq.QuadPart;
    s_last = now;

    if (dt < 0.001f) { dt = 0.001f; }
    if (dt > 0.100f) { dt = 0.100f; }
    return dt;
}

/* 睡到下一帧（粗略即可）*/
static void timer_sleep_rest(int ms)
{
    Sleep((DWORD)(ms > 0 ? ms : 1));
}

/* ============================================================================
 * RGB565 -> 32bpp BGRA
 * ========================================================================== */

static void convert_565_to_bgra(void)
{
    const uint16_t *src = g_pc_fb;
    uint32_t *dst = s_bgra;
    int i;

    for (i = 0; i < WIN_W * WIN_H; i++)
    {
        uint32_t c = src[i];
        uint32_t r = (c >> 11) & 0x1Fu;
        uint32_t g = (c >>  5) & 0x3Fu;
        uint32_t b =  c        & 0x1Fu;

        /* 5/6/5 位扩展到 8 位：复制高位，比乘法快且不会过冲 */
        r = (r << 3) | (r >> 2);
        g = (g << 2) | (g >> 4);
        b = (b << 3) | (b >> 2);

        dst[i] = (r << 16) | (g << 8) | b;
    }
}

/* 把 565 值转成 GDI 的 COLORREF */
static COLORREF colorref_from_565(uint16_t c)
{
    uint32_t r = (c >> 11) & 0x1Fu;
    uint32_t g = (c >>  5) & 0x3Fu;
    uint32_t b =  c        & 0x1Fu;

    r = (r << 3) | (r >> 2);
    g = (g << 2) | (g >> 4);
    b = (b << 3) | (b >> 2);

    return RGB(r, g, b);
}

/* ============================================================================
 * 文字用 GDI 画
 * ========================================================================== */

static void draw_glyphs(HDC hdc)
{
    static PcGlyph buf[PC_MAX_GLYPHS];
    int n = Pc_TakeGlyphs(buf, PC_MAX_GLYPHS);
    int i;

    if (n <= 0) { return; }

    SetBkMode(hdc, TRANSPARENT);

    for (i = 0; i < n; i++)
    {
        SetTextColor(hdc, colorref_from_565(buf[i].color565));
        TextOutA(hdc, buf[i].x, buf[i].y, &buf[i].ch, 1);
    }
}

/* ============================================================================
 * 键盘
 * ========================================================================== */

static int key_down(int vk)
{
    return (GetAsyncKeyState(vk) & 0x8000) ? 1 : 0;
}

/* 按住就往一个方向加，松开就往 0 回 */
static float ramp_tilt(float v, float dir, float dt)
{
    if (dir > 0.0f)
    {
        v += TILT_RAMP * dt;
        if (v > TILT_MAX) { v = TILT_MAX; }
    }
    else if (dir < 0.0f)
    {
        v -= TILT_RAMP * dt;
        if (v < -TILT_MAX) { v = -TILT_MAX; }
    }
    else
    {
        float step = TILT_DECAY * dt;
        if (v > step)       { v -= step; }
        else if (v < -step) { v += step; }
        else                { v = 0.0f; }
    }
    return v;
}

static void update_tilt(float dt)
{
    float dx = 0.0f, dy = 0.0f;

    if (key_down('D')) { dx += 1.0f; }
    if (key_down('A')) { dx -= 1.0f; }
    if (key_down('S')) { dy += 1.0f; }
    if (key_down('W')) { dy -= 1.0f; }

    s_tx = ramp_tilt(s_tx, dx, dt);
    s_ty = ramp_tilt(s_ty, dy, dt);
}

/* 1 = KEY1（开始/暂停/继续/下一关/重试），2 = KEY2（重玩本关）*/
static void poll_keys(App_Ctx *app)
{
    int k1 = key_down('1');
    int k2 = key_down('2');

    if (k1 && !s_k1_last)
    {
        if (app->button_count > 0)
        {
            App_DoAction(app, app->buttons[0].action);
            Render_Begin(app);          /* 状态变了，静态层重画 */
        }
    }
    if (k2 && !s_k2_last)
    {
        App_DoAction(app, ACT_RESTART);
        Render_Begin(app);
    }

    s_k1_last = k1;
    s_k2_last = k2;

    if (key_down(VK_ESCAPE)) { s_running = 0; }
}

/* ============================================================================
 * 窗口
 * ========================================================================== */

static void update_title(App_Ctx *app)
{
    char buf[160];

    snprintf(buf, sizeof(buf), "LEVEL %u/%u   %s   %lu.%lus   WALL %u",
             (unsigned)(app->game.level_index + 1),
             (unsigned)GAME_LEVEL_COUNT,
             App_StateName(app),
             (unsigned long)(app->game.elapsed_ms / 1000u),
             (unsigned long)((app->game.elapsed_ms % 1000u) / 100u),
             (unsigned)Game_GetLevel(&app->game)->wall_count);

    SetWindowTextA(s_hwnd, buf);
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_DESTROY:
        s_running = 0;
        PostQuitMessage(0);
        return 0;

    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) { s_running = 0; }
        return 0;
    }

    return DefWindowProcA(hwnd, msg, wp, lp);
}

static void poll_messages(void)
{
    MSG msg;

    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE))
    {
        if (msg.message == WM_QUIT) { s_running = 0; break; }
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
}

/* ============================================================================
 * main
 * ========================================================================== */

int main(void)
{
    static App_Ctx app;
    WNDCLASSEXA wc;
    RECT rc;
    HINSTANCE hinst = GetModuleHandleA(NULL);
    HDC hdc;

    /* ---------- 1. 注册并创建窗口（客户区恰好 800x480）---------- */
    memset(&wc, 0, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = wnd_proc;
    wc.hInstance     = hinst;
    wc.hCursor       = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = "PcPreview";
    if (!RegisterClassExA(&wc))
    {
        printf("RegisterClassEx failed: %lu\n", GetLastError());
        return 1;
    }

    rc.left = 0; rc.top = 0; rc.right = WIN_W; rc.bottom = WIN_H;
    AdjustWindowRectEx(&rc,
                       (WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX),
                       FALSE, 0);

    s_hwnd = CreateWindowExA(0, "PcPreview", "Tilt Game Preview",
                             (WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX),
                             CW_USEDEFAULT, CW_USEDEFAULT,
                             rc.right - rc.left, rc.bottom - rc.top,
                             NULL, NULL, hinst, NULL);
    if (s_hwnd == NULL)
    {
        printf("CreateWindowEx failed: %lu\n", GetLastError());
        return 1;
    }

    hdc = GetDC(s_hwnd);
    if (hdc == NULL) { printf("GetDC failed\n"); return 1; }

    /* ---------- 2. 字体（等宽，22px 接近 Font16x24）---------- */
    s_font = CreateFontA(22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                         ANTIALIASED_QUALITY, FF_MODERN, "Consolas");
    if (s_font) { SelectObject(hdc, s_font); }

    /* ---------- 3. 位图头 ---------- */
    memset(&s_bmi, 0, sizeof(s_bmi));
    s_bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    s_bmi.bmiHeader.biWidth       = WIN_W;
    s_bmi.bmiHeader.biHeight      = -WIN_H;     /* ⚠️ 负 = 自上而下 */
    s_bmi.bmiHeader.biPlanes      = 1;
    s_bmi.bmiHeader.biBitCount    = 32;
    s_bmi.bmiHeader.biCompression = BI_RGB;

    ShowWindow(s_hwnd, SW_SHOW);
    UpdateWindow(s_hwnd);

    /* ---------- 4. 游戏初始化（复用固件的模块）---------- */
    App_Init(&app);
    Render_Begin(&app);                 /* 第一帧画进 g_pc_fb */

    printf("PC preview started.\n");
    printf("  WASD = tilt  1 = KEY1  2 = KEY2  ESC = quit\n");
    fflush(stdout);

    timer_init();

    /* ---------- 5. 主循环 ---------- */
    while (s_running)
    {
        float dt;
        LARGE_INTEGER t0;

        QueryPerformanceCounter(&t0);

        dt = timer_dt();

        poll_messages();
        if (!s_running) { break; }

        poll_keys(&app);
        update_tilt(dt);

        App_Update(&app, s_tx, s_ty, dt);
        Render_Frame(&app);

        convert_565_to_bgra();

        StretchDIBits(hdc, 0, 0, WIN_W, WIN_H, 0, 0, WIN_W, WIN_H,
                      s_bgra, &s_bmi, DIB_RGB_COLORS, SRCCOPY);

        draw_glyphs(hdc);

        update_title(&app);

        /* 补足 33ms */
        {
            LARGE_INTEGER t1;
            long elapsed_ms;
            QueryPerformanceCounter(&t1);
            elapsed_ms = (long)((t1.QuadPart - t0.QuadPart) * 1000 /
                                s_freq.QuadPart);
            timer_sleep_rest((int)(FRAME_MS - elapsed_ms));
        }
    }

    if (s_font) { DeleteObject(s_font); }
    ReleaseDC(s_hwnd, hdc);
    printf("bye.\n");
    return 0;
}
