/**
 ******************************************************************************
 * @file    fbview.c
 * @brief   PCFbView 宿主核心：帧缓冲 + 窗口 + 输入 + 计时 + 文字 + 导出
 *
 * 只依赖 windows.h / gdi32 / user32。
 * 具体画什么由适配器 DLL 决定。
 ******************************************************************************
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fbview_host.h"

/* ============================================================================
 * 状态
 * ========================================================================== */

static uint16_t  *s_fb   = NULL;
static int        s_w    = 0;
static int        s_h    = 0;

static uint32_t  *s_bgra = NULL;
static BITMAPINFO s_bmi;
static HFONT      s_font = NULL;
static HWND       s_hwnd = NULL;
static HDC        s_hdc  = NULL;

static FvAdapter *s_ad   = NULL;
static FvHost     s_host;

static int        s_running = 1;

static float      s_tilt_max   = 6300.0f;
static float      s_tilt_ramp  = 18000.0f;
static float      s_tilt_decay = 24000.0f;
static float      s_tx = 0.0f, s_ty = 0.0f;

static int        s_k1_last = 0;
static int        s_k2_last = 0;

static LARGE_INTEGER s_freq, s_last;

/* ---------------- 文字队列 ---------------- */
#define FV_MAX_TEXT_RUNS   512

typedef struct { int x, y; uint16_t c565; char s[96]; } FvTextRun;
static FvTextRun s_texts[FV_MAX_TEXT_RUNS];
static int       s_text_count = 0;

void Fv_HostDrawText(const char *s, int x, int y, uint16_t c565)
{
    int n;

    if (s == NULL || s_text_count >= FV_MAX_TEXT_RUNS) { return; }

    n = (int)strlen(s);
    if (n > (int)sizeof(s_texts[0].s) - 1) { n = (int)sizeof(s_texts[0].s) - 1; }

    s_texts[s_text_count].x    = x;
    s_texts[s_text_count].y    = y;
    s_texts[s_text_count].c565 = c565;
    memcpy(s_texts[s_text_count].s, s, (size_t)n);
    s_texts[s_text_count].s[n] = '\0';
    s_text_count++;
}

static COLORREF colorref_from_565(uint16_t c)
{
    unsigned int r = (c >> 11) & 0x1F;
    unsigned int g = (c >>  5) & 0x3F;
    unsigned int b =  c        & 0x1F;

    r = (r << 3) | (r >> 2);
    g = (g << 2) | (g >> 4);
    b = (b << 3) | (b >> 2);

    return RGB(r, g, b);
}

static void flush_texts(HDC hdc)
{
    int i;

    if (s_text_count <= 0) { return; }

    SetBkMode(hdc, TRANSPARENT);
    for (i = 0; i < s_text_count; i++)
    {
        SetTextColor(hdc, colorref_from_565(s_texts[i].c565));
        TextOutA(hdc, s_texts[i].x, s_texts[i].y,
                 s_texts[i].s, (int)strlen(s_texts[i].s));
    }
    s_text_count = 0;
}

/* ============================================================================
 * 时间
 * ========================================================================== */

static void timer_init(void)
{
    QueryPerformanceFrequency(&s_freq);
    QueryPerformanceCounter(&s_last);
}

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

/* ============================================================================
 * 显示
 * ========================================================================== */

static void convert_565_to_bgra(void)
{
    int i, n = s_w * s_h;

    for (i = 0; i < n; i++)
    {
        uint32_t c = s_fb[i];
        uint32_t r = (c >> 11) & 0x1Fu;
        uint32_t g = (c >>  5) & 0x3Fu;
        uint32_t b =  c        & 0x1Fu;

        r = (r << 3) | (r >> 2);
        g = (g << 2) | (g >> 4);
        b = (b << 3) | (b >> 2);

        s_bgra[i] = (r << 16) | (g << 8) | b;
    }
}

/* ============================================================================
 * 输入
 * ========================================================================== */

static int key_down(int vk) { return (GetAsyncKeyState(vk) & 0x8000) ? 1 : 0; }

static float ramp_tilt(float v, float dir, float dt)
{
    if (dir > 0.0f)
    {
        v += s_tilt_ramp * dt;
        if (v >  s_tilt_max) { v =  s_tilt_max; }
    }
    else if (dir < 0.0f)
    {
        v -= s_tilt_ramp * dt;
        if (v < -s_tilt_max) { v = -s_tilt_max; }
    }
    else
    {
        float step = s_tilt_decay * dt;
        if (v > step)       { v -= step; }
        else if (v < -step) { v += step; }
        else                { v = 0.0f; }
    }
    return v;
}

static void poll_input(float dt)
{
    float dx = 0.0f, dy = 0.0f;
    int k1, k2;

    if (key_down('D')) { dx += 1.0f; }
    if (key_down('A')) { dx -= 1.0f; }
    if (key_down('S')) { dy += 1.0f; }
    if (key_down('W')) { dy -= 1.0f; }

    s_tx = ramp_tilt(s_tx, dx, dt);
    s_ty = ramp_tilt(s_ty, dy, dt);

    s_host.tilt_x = s_tx;
    s_host.tilt_y = s_ty;

    k1 = key_down('1');
    k2 = key_down('2');

    if (k1 && !s_k1_last && s_ad && s_ad->Key) { s_ad->Key(1); }
    if (k2 && !s_k2_last && s_ad && s_ad->Key) { s_ad->Key(2); }

    s_k1_last = k1;
    s_k2_last = k2;

    if (key_down(VK_ESCAPE)) { s_running = 0; }
}

/* ============================================================================
 * 窗口
 * ========================================================================== */

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
 * 导出 BMP
 * ========================================================================== */

static int export_bmp(const char *path)
{
    FILE *fp;
    int x, y, row_size, pad_size;
    unsigned char hdr[54];
    unsigned char *row;
    unsigned int file_size;

    if (s_fb == NULL) { return -1; }

    fp = fopen(path, "wb");
    if (fp == NULL) { return -1; }

    row_size  = s_w * 3;
    pad_size  = (4 - (row_size % 4)) % 4;
    file_size = 54u + (unsigned int)(row_size + pad_size) * (unsigned int)s_h;

    memset(hdr, 0, sizeof(hdr));
    hdr[0] = 'B'; hdr[1] = 'M';
    hdr[2]  = (unsigned char)(file_size);
    hdr[3]  = (unsigned char)(file_size >> 8);
    hdr[4]  = (unsigned char)(file_size >> 16);
    hdr[5]  = (unsigned char)(file_size >> 24);
    hdr[10] = 54;
    hdr[14] = 40;
    hdr[18] = (unsigned char)(s_w);
    hdr[19] = (unsigned char)(s_w >> 8);
    hdr[20] = (unsigned char)(s_w >> 16);
    hdr[21] = (unsigned char)(s_w >> 24);
    hdr[22] = (unsigned char)(s_h);
    hdr[23] = (unsigned char)(s_h >> 8);
    hdr[24] = (unsigned char)(s_h >> 16);
    hdr[25] = (unsigned char)(s_h >> 24);
    hdr[26] = 1;
    hdr[28] = 24;
    hdr[34] = (unsigned char)((row_size + pad_size) * s_h);
    hdr[35] = (unsigned char)(((row_size + pad_size) * s_h) >> 8);
    hdr[36] = (unsigned char)(((row_size + pad_size) * s_h) >> 16);
    hdr[37] = (unsigned char)(((row_size + pad_size) * s_h) >> 24);
    fwrite(hdr, 1, 54, fp);

    row = (unsigned char *)malloc((size_t)row_size);
    if (row == NULL) { fclose(fp); return -1; }

    for (y = s_h - 1; y >= 0; y--)
    {
        for (x = 0; x < s_w; x++)
        {
            unsigned short c = s_fb[(size_t)y * s_w + x];
            unsigned int r = (c >> 11) & 0x1F;
            unsigned int g = (c >>  5) & 0x3F;
            unsigned int b =  c        & 0x1F;

            r = (r << 3) | (r >> 2);
            g = (g << 2) | (g >> 4);
            b = (b << 3) | (b >> 2);

            row[x * 3 + 0] = (unsigned char)b;
            row[x * 3 + 1] = (unsigned char)g;
            row[x * 3 + 2] = (unsigned char)r;
        }
        fwrite(row, 1, (size_t)row_size, fp);
        if (pad_size > 0)
        {
            unsigned char pad[4] = { 0, 0, 0, 0 };
            fwrite(pad, 1, (size_t)pad_size, fp);
        }
    }

    free(row);
    fclose(fp);
    return 0;
}

/* ============================================================================
 * 共用初始化
 * ========================================================================== */

static int alloc_fb(int w, int h)
{
    s_w  = w;
    s_h  = h;
    s_fb = (uint16_t *)calloc((size_t)w * h, sizeof(uint16_t));
    if (s_fb == NULL) { return -1; }

    if (s_bgra == NULL)
    {
        s_bgra = (uint32_t *)calloc((size_t)w * h, sizeof(uint32_t));
        if (s_bgra == NULL) { return -1; }
    }

    memset(&s_host, 0, sizeof(s_host));
    s_host.fb        = s_fb;
    s_host.w         = w;
    s_host.h         = h;
    s_host.draw_text = Fv_HostDrawText;
    return 0;
}

static void free_fb(void)
{
    if (s_fb)   { free(s_fb);   s_fb   = NULL; }
    if (s_bgra) { free(s_bgra); s_bgra = NULL; }
}

/* ============================================================================
 * 对外接口
 * ========================================================================== */

int Fv_RunHeadless(int w, int h, FvAdapter *ad, const char *bmp_path)
{
    int ret;

    if (ad == NULL || bmp_path == NULL) { return -1; }
    if (alloc_fb(w, h) != 0) { return -1; }

    if (ad->Attach(&s_host) != 0)
    {
        printf("Pv_Attach failed\n");
        free_fb();
        return -1;
    }

    ad->Update(0.0f, 0.0f, 0.033f);
    ad->Render();

    ret = export_bmp(bmp_path);
    if (ret == 0) { printf("exported %s (%dx%d)\n", bmp_path, w, h); }

    free_fb();
    return ret;
}

int Fv_RunHost(int w, int h, int fps, FvAdapter *ad, const char *title)
{
    WNDCLASSEXA wc;
    RECT rc;
    HINSTANCE hinst;
    long frame_ms;

    if (ad == NULL || w <= 0 || h <= 0) { return -1; }
    if (alloc_fb(w, h) != 0) { return -1; }

    s_host.fps = fps;

    hinst = GetModuleHandleA(NULL);
    memset(&wc, 0, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = wnd_proc;
    wc.hInstance     = hinst;
    wc.hCursor       = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = "PcFbViewHost";
    if (!RegisterClassExA(&wc))
    {
        printf("RegisterClassEx failed: %lu\n", GetLastError());
        free_fb();
        return -1;
    }

    rc.left = 0; rc.top = 0; rc.right = w; rc.bottom = h;
    AdjustWindowRectEx(&rc, (WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX),
                       FALSE, 0);

    s_hwnd = CreateWindowExA(0, "PcFbViewHost",
                             (title != NULL) ? title : "PCFbView",
                             (WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX),
                             CW_USEDEFAULT, CW_USEDEFAULT,
                             rc.right - rc.left, rc.bottom - rc.top,
                             NULL, NULL, hinst, NULL);
    if (s_hwnd == NULL)
    {
        printf("CreateWindowEx failed: %lu\n", GetLastError());
        free_fb();
        return -1;
    }

    s_hdc = GetDC(s_hwnd);
    if (s_hdc == NULL) { free_fb(); return -1; }

    s_font = CreateFontA(22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                         ANTIALIASED_QUALITY, FF_MODERN, "Consolas");
    if (s_font) { SelectObject(s_hdc, s_font); }

    memset(&s_bmi, 0, sizeof(s_bmi));
    s_bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    s_bmi.bmiHeader.biWidth       = w;
    s_bmi.bmiHeader.biHeight      = -h;        /* negative = top-down */
    s_bmi.bmiHeader.biPlanes      = 1;
    s_bmi.bmiHeader.biBitCount    = 32;
    s_bmi.bmiHeader.biCompression = BI_RGB;

    ShowWindow(s_hwnd, SW_SHOW);
    UpdateWindow(s_hwnd);

    if (ad->Attach(&s_host) != 0)
    {
        printf("Pv_Attach failed, abort\n");
        free_fb();
        return -1;
    }

    printf("PCFbView %dx%d @%dfps\n", w, h, fps);
    printf("  WASD = tilt   1 = KEY1   2 = KEY2   ESC = quit\n");
    fflush(stdout);

    s_ad = ad;
    timer_init();
    frame_ms = (long)(1000 / (fps > 0 ? fps : 30));

    while (s_running)
    {
        LARGE_INTEGER t0, t1;
        float dt;
        long used_ms;
        const char *t;

        QueryPerformanceCounter(&t0);
        dt = timer_dt();

        poll_messages();
        if (!s_running) { break; }

        poll_input(dt);

        ad->Update(s_tx, s_ty, dt);
        ad->Render();

        convert_565_to_bgra();
        StretchDIBits(s_hdc, 0, 0, w, h, 0, 0, w, h,
                      s_bgra, &s_bmi, DIB_RGB_COLORS, SRCCOPY);
        flush_texts(s_hdc);

        if (ad->Title != NULL)
        {
            t = ad->Title();
            if (t != NULL && s_hwnd != NULL) { SetWindowTextA(s_hwnd, t); }
        }

        QueryPerformanceCounter(&t1);
        used_ms = (long)((t1.QuadPart - t0.QuadPart) * 1000 / s_freq.QuadPart);
        if (used_ms < frame_ms) { Sleep((DWORD)(frame_ms - used_ms)); }
    }

    if (s_font) { DeleteObject(s_font); }
    if (s_hdc)  { ReleaseDC(s_hwnd, s_hdc); }
    free_fb();
    s_ad = NULL;
    printf("bye.\n");
    return 0;
}
