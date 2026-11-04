/* Win32 preview host. The adapter ABI stays in fbview.h; display contention
 * is isolated in fv_sim.c so its assumptions and thresholds are testable. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <commctrl.h>
#include <windowsx.h>
#include "fv_terminal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fbview_host.h"

#define TOP 40
#define BOTTOM 48
#define PROJECT_ID 101
#define ADD_ID 102
#define LIMIT_ID 103
#define MAX_PROJECTS 64
#define PANEL 270
#define LOG_HEIGHT 180
#define SENSOR_BASE 201
#define RESET_ID 210

typedef struct {
    uint16_t *fb;
    uint32_t *pixels;
    HDC dc;
    HBITMAP bitmap;
    HGDIOBJ previous;
    HFONT font;
    HGDIOBJ previous_font;
    int w, h;
} Surface;
static Surface s_surface;
static HWND s_hwnd, s_projects, s_add, s_limit, s_status;
static HWND s_terminal, s_terminal_title, s_sensor_title, s_sensor_help, s_reset;
static HWND s_sensor_labels[3], s_sliders[3];
static const char *s_sensor_names[3] = {"Temperature", "Humidity", "Light"};
static const char *s_sensor_units[3] = {"C", "%", "lux"};
static const int s_sensor_min[3] = {-40, 0, 0};
static const int s_sensor_max[3] = {125, 100, 10000};
static const int s_sensor_default[3] = {25, 50, 500};
static int s_sensor_values[3] = {25, 50, 500};
static int s_touching;
static FvAdapter *s_ad;
static void refresh_sensors(int reset);
static FvViewport preview_viewport(void);
static FvHost s_host;
static FvConfig s_cfg;
static char s_folder[MAX_PATH];
static char s_folders[MAX_PROJECTS][MAX_PATH];
static int s_project_count, s_current, s_pending = -1;
static int s_running = 1, s_paused, s_debug, s_want_reload, s_want_shot;
static float s_tx, s_ty;
static float s_tilt_max = 6300, s_tilt_ramp = 18000, s_tilt_decay = 24000;
static int s_k1_last, s_k2_last, s_shot_no;
static double s_fps_now, s_last_ms;
static LARGE_INTEGER s_freq, s_last;
static FvMemoryStats s_stats;
static unsigned long s_underrun_frames;
static const double s_limits[] = {0, 120, 80, 60, 40, 20, 5};
static const char *s_limit_names[] = {"SDRAM: unlimited", "SDRAM: 120 MB/s", "SDRAM: 80 MB/s", "SDRAM: 60 MB/s", "SDRAM: 40 MB/s", "SDRAM: 20 MB/s", "SDRAM: 5 MB/s"};

typedef struct { int x, y; uint16_t color; char text[256]; } Text;
static Text s_texts[1024];
static int s_text_count;

static void destroy_surface(Surface *s)
{
    if (s->dc) {
        SelectObject(s->dc, s->previous);
        if (s->font) SelectObject(s->dc, s->previous_font);
    }
    if (s->font) DeleteObject(s->font);
    if (s->bitmap) DeleteObject(s->bitmap);
    if (s->dc) DeleteDC(s->dc);
    free(s->fb);
    memset(s, 0, sizeof(*s));
}
static int create_surface(Surface *s, int w, int h)
{
    BITMAPINFO bi = {0};
    memset(s, 0, sizeof(*s));
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096) return -1;
    s->w = w; s->h = h;
    s->fb = calloc((size_t)w * h, sizeof(uint16_t));
    s->dc = CreateCompatibleDC(NULL);
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    s->bitmap = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, (void **)&s->pixels, NULL, 0);
    if (!s->fb || !s->dc || !s->bitmap) { destroy_surface(s); return -1; }
    s->previous = SelectObject(s->dc, s->bitmap);
    s->font = CreateFontA(22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY, FF_MODERN, "Consolas");
    if (s->font) s->previous_font = SelectObject(s->dc, s->font);
    return 0;
}
static void bind_surface(void)
{
    memset(&s_host, 0, sizeof(s_host));
    s_host.fb = s_surface.fb; s_host.w = s_surface.w; s_host.h = s_surface.h;
    s_host.fps = s_cfg.fps; s_host.draw_text = Fv_HostDrawText;
}
void Fv_HostDrawText(const char *text, int x, int y, uint16_t color)
{
    Text *t;
    if (!text || s_text_count >= 1024) return;
    t = &s_texts[s_text_count++];
    t->x = x; t->y = y; t->color = color;
    snprintf(t->text, sizeof(t->text), "%s", text);
}
static uint32_t rgb565(uint16_t c)
{
    unsigned r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    return (((r << 3) | (r >> 2)) << 16) | (((g << 2) | (g >> 4)) << 8) | (b << 3) | (b >> 2);
}
static void compose(double render_hz)
{
    size_t i, count = (size_t)s_surface.w * s_surface.h;
    for (i = 0; i < count; ++i) s_surface.pixels[i] = rgb565(s_surface.fb[i]);
    SetBkMode(s_surface.dc, TRANSPARENT);
    for (int j = 0; j < s_text_count; ++j) {
        Text *t = &s_texts[j]; uint32_t c = rgb565(t->color);
        SetTextColor(s_surface.dc, RGB((c >> 16) & 255, (c >> 8) & 255, c & 255));
        TextOutA(s_surface.dc, t->x, t->y, t->text, (int)strlen(t->text));
    }
    GdiFlush(); /* CPU accesses the DIB only after queued GDI writes complete. */
    /* Firmware renderers may draw labels only when a screen changes. Bake the
       glyph pixels into RGB565 before clearing the transient command queue. */
    if (s_text_count) for (i = 0; i < count; ++i) {
        uint32_t c = s_surface.pixels[i];
        s_surface.fb[i] = (uint16_t)(((c >> 8) & 0xf800) | ((c >> 5) & 0x07e0) | ((c >> 3) & 0x001f));
    }
    s_text_count = 0;
    s_stats = Fv_ApplyMemory(s_surface.pixels, count, render_hz, &s_cfg.memory);
    if (s_stats.underrun) ++s_underrun_frames;
}
static int export_bmp(const char *path)
{
    BITMAPFILEHEADER fh = {0}; BITMAPINFOHEADER ih = {0};
    size_t bytes = (size_t)s_surface.w * s_surface.h * 4;
    FILE *fp = fopen(path, "wb"); int ok;
    if (!fp) return -1;
    fh.bfType = 0x4d42; fh.bfOffBits = sizeof(fh) + sizeof(ih);
    fh.bfSize = fh.bfOffBits + (DWORD)bytes;
    ih.biSize = sizeof(ih); ih.biWidth = s_surface.w; ih.biHeight = -s_surface.h;
    ih.biPlanes = 1; ih.biBitCount = 32; ih.biCompression = BI_RGB; ih.biSizeImage = (DWORD)bytes;
    ok = fwrite(&fh, sizeof(fh), 1, fp) == 1 && fwrite(&ih, sizeof(ih), 1, fp) == 1 &&
         fwrite(s_surface.pixels, 1, bytes, fp) == bytes;
    if (fclose(fp) != 0) ok = 0;
    return ok ? 0 : -1;
}
static void take_screenshot(void)
{
    char dir[MAX_PATH], path[MAX_PATH], *p;
    GetModuleFileNameA(NULL, dir, sizeof(dir));
    p = strrchr(dir, '\\'); if (p) *p = 0;
    if (strlen(dir) + 50 >= MAX_PATH) return;
    strcat(dir, "\\screenshots"); CreateDirectoryA(dir, NULL);
    do { snprintf(path, sizeof(path), "%s\\shot_%04d.bmp", dir, ++s_shot_no); }
    while (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES);
    printf("screenshot %s: %s\n", export_bmp(path) == 0 ? "saved" : "failed", path);
}
static void timer_init(void)
{
    QueryPerformanceFrequency(&s_freq); QueryPerformanceCounter(&s_last);
}
static float timer_dt(void)
{
    LARGE_INTEGER now; float dt;
    QueryPerformanceCounter(&now);
    dt = (float)((double)(now.QuadPart - s_last.QuadPart) / s_freq.QuadPart); s_last = now;
    if (dt < .001f) dt = .001f;
    if (dt > .1f) dt = .1f;
    return dt;
}
static int key_down(int vk)
{
    return GetForegroundWindow() == s_hwnd && GetFocus() == s_hwnd &&
           (GetAsyncKeyState(vk) & 0x8000) != 0;
}

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

    /* ---- 编辑键：都用"按下那一刻"触发，避免按住连发 ---- */
    {
        static int l_f1 = 0, l_sp = 0, l_f11 = 0, l_f12 = 0, l_r = 0;
        int f1  = key_down(VK_F1);
        int sp  = key_down(VK_SPACE);
        int f11 = key_down(VK_F11);
        int f12 = key_down(VK_F12);
        int rr  = key_down('R');

        if (f1  && !l_f1)  { s_debug  = !s_debug; }
        if (sp  && !l_sp)  { s_paused = !s_paused; }
        if (f12 && !l_f12) { s_want_shot = 1; }
        if (rr  && !l_r)   { s_want_reload = 1; }

        if (f11 && !l_f11)
        {
            static int full = 0;
            static WINDOWPLACEMENT wp;
            full = !full;
            if (full)
            {
                wp.length = sizeof(wp);
                GetWindowPlacement(s_hwnd, &wp);
                /* 去掉标题栏和边框，铺满屏幕 */
                SetWindowLongA(s_hwnd, GWL_STYLE,
                               GetWindowLongA(s_hwnd, GWL_STYLE) & ~WS_OVERLAPPEDWINDOW);
                SetWindowPos(s_hwnd, HWND_TOP, 0, 0,
                             GetSystemMetrics(SM_CXSCREEN),
                             GetSystemMetrics(SM_CYSCREEN),
                             SWP_FRAMECHANGED | SWP_SHOWWINDOW);
            }
            else
            {
                SetWindowLongA(s_hwnd, GWL_STYLE,
                               GetWindowLongA(s_hwnd, GWL_STYLE) | WS_OVERLAPPEDWINDOW);
                SetWindowPlacement(s_hwnd, &wp);
                SetWindowPos(s_hwnd, NULL, 0, 0, 0, 0,
                             SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
            }
        }

        l_f1 = f1; l_sp = sp; l_f11 = f11; l_f12 = f12; l_r = rr;
    }

    if (key_down(VK_ESCAPE)) { s_running = 0; }
}


int Fv_AddProject(const char *folder)
{
    char full[MAX_PATH]; DWORD n;
    n = GetFullPathNameA(folder, sizeof(full), full, NULL);
    if (!n || n >= sizeof(full)) return -1;
    while (strlen(full) > 3 && full[strlen(full)-1] == '\\') full[strlen(full)-1] = 0;
    for (int i = 0; i < s_project_count; ++i) if (_stricmp(full, s_folders[i]) == 0) return i;
    if (s_project_count == MAX_PROJECTS) return -1;
    strcpy(s_folders[s_project_count], full);
    if (s_projects) SendMessageA(s_projects, CB_ADDSTRING, 0, (LPARAM)full);
    return s_project_count++;
}
static FvViewport preview_viewport(void)
{
    RECT rc; GetClientRect(s_hwnd, &rc);
    return Fv_FitViewport(rc.right - PANEL - 16, rc.bottom, TOP,
                          BOTTOM + LOG_HEIGHT, s_surface.w, s_surface.h);
}
static void refresh_sensors(int reset)
{
    int supported = s_ad && s_ad->SetSensor;
    if (s_sensor_help) SetWindowTextW(s_sensor_help, supported ?
        L"Sensor inputs are connected to this project." :
        L"This project has no sensor input interface. Select Demo to try the sliders.");
    for (int i = 0; i < 3; ++i) {
        char text[96];
        if (reset) s_sensor_values[i] = s_sensor_default[i];
        if (s_sliders[i]) {
            SendMessageA(s_sliders[i], TBM_SETPOS, TRUE, s_sensor_values[i]);
            EnableWindow(s_sliders[i], supported);
        }
        snprintf(text, sizeof(text), "%s: %d %s", s_sensor_names[i], s_sensor_values[i], s_sensor_units[i]);
        if (s_sensor_labels[i]) SetWindowTextA(s_sensor_labels[i], text);
        if (supported) s_ad->SetSensor(i + 1, (float)s_sensor_values[i]);
    }
    if (s_reset) EnableWindow(s_reset, supported);
}
static void layout(HWND hwnd)
{
    RECT rc; int pw, left, right, log_y;
    GetClientRect(hwnd, &rc); pw = rc.right - 340; if (pw < 100) pw = 100;
    left = rc.right - PANEL - 16; right = left + 16;
    log_y = rc.bottom - LOG_HEIGHT;
    MoveWindow(s_projects, 8, 7, pw, 400, TRUE);
    MoveWindow(s_add, pw + 16, 7, 100, 26, TRUE);
    MoveWindow(s_limit, pw + 124, 7, 208, 260, TRUE);
    MoveWindow(s_status, 8, log_y - BOTTOM + 3, left - 16, BOTTOM - 6, TRUE);
    MoveWindow(s_terminal_title, 8, log_y, left - 16, 22, TRUE);
    MoveWindow(s_terminal, 8, log_y + 24, left - 16, LOG_HEIGHT - 32, TRUE);
    MoveWindow(s_sensor_title, right, TOP + 8, PANEL - 16, 26, TRUE);
    MoveWindow(s_sensor_help, right, TOP + 40, PANEL - 16, 60, TRUE);
    for (int i = 0; i < 3; ++i) {
        MoveWindow(s_sensor_labels[i], right, TOP + 118 + i * 100, PANEL - 16, 24, TRUE);
        MoveWindow(s_sliders[i], right, TOP + 148 + i * 100, PANEL - 16, 36, TRUE);
    }
    MoveWindow(s_reset, right, TOP + 430, PANEL - 16, 30, TRUE);
    InvalidateRect(hwnd, NULL, FALSE);
}
static void paint(HDC dc)
{
    RECT rc; FvViewport v;
    GetClientRect(s_hwnd, &rc);
    FillRect(dc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
    v = preview_viewport();
    if (s_surface.dc && v.w > 0 && v.h > 0) {
        SetStretchBltMode(dc, COLORONCOLOR);
        StretchBlt(dc, v.x, v.y, v.w, v.h, s_surface.dc, 0, 0,
                   s_surface.w, s_surface.h, SRCCOPY);
    }
}
static int touch_position(LPARAM lp, int *x, int *y)
{
    FvViewport v = preview_viewport();
    return Fv_MapTouch(v, s_surface.w, s_surface.h, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), x, y);
}
static void pick_project(void)
{
    BROWSEINFOA bi = {0}; LPITEMIDLIST id; char folder[MAX_PATH];
    bi.hwndOwner = s_hwnd; bi.lpszTitle = "Choose a project with preview.ini or adapter DLL";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    id = SHBrowseForFolderA(&bi);
    if (id) {
        if (SHGetPathFromIDListA(id, folder)) s_pending = Fv_AddProject(folder);
        CoTaskMemFree(id);
    }
    SetFocus(s_hwnd);
}
static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_DESTROY: s_running = 0; PostQuitMessage(0); return 0;
    case WM_GETMINMAXINFO:
        ((MINMAXINFO *)lp)->ptMinTrackSize.x = 980;
        ((MINMAXINFO *)lp)->ptMinTrackSize.y = 680; return 0;
    case WM_SIZE: if (s_projects) layout(hwnd); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(hwnd, &ps); paint(dc); EndPaint(hwnd, &ps); return 0;
    }
    case WM_HSCROLL:
        for (int i = 0; i < 3; ++i) if ((HWND)lp == s_sliders[i]) {
            s_sensor_values[i] = (int)SendMessageA(s_sliders[i], TBM_GETPOS, 0, 0);
            refresh_sensors(0);
        }
        return 0;
    case WM_LBUTTONDOWN: {
        int x, y;
        SetFocus(hwnd);
        if (s_ad && s_ad->Touch && touch_position(lp, &x, &y)) {
            s_touching = 1; SetCapture(hwnd); s_ad->Touch(x, y, 1);
        }
        return 0;
    }
    case WM_MOUSEMOVE:
        if (s_touching && s_ad && s_ad->Touch) {
            int x, y; if (touch_position(lp, &x, &y)) s_ad->Touch(x, y, 1);
        }
        return 0;
    case WM_LBUTTONUP:
        if (s_touching) {
            int x = 0, y = 0, inside = touch_position(lp, &x, &y);
            s_touching = 0; ReleaseCapture();
            if (s_ad && s_ad->Touch) s_ad->Touch(x, y, inside ? 0 : -1);
        }
        return 0;
    case WM_CAPTURECHANGED:
        if (s_touching && s_ad && s_ad->Touch) s_ad->Touch(0, 0, -1);
        s_touching = 0; return 0;
    case WM_COMMAND:
        if (LOWORD(wp) == RESET_ID && HIWORD(wp) == BN_CLICKED) { refresh_sensors(1); SetFocus(hwnd); }
        if (LOWORD(wp) == ADD_ID && HIWORD(wp) == BN_CLICKED) pick_project();
        if (LOWORD(wp) == PROJECT_ID && HIWORD(wp) == CBN_SELENDOK) {
            s_pending = (int)SendMessageA(s_projects, CB_GETCURSEL, 0, 0); SetFocus(hwnd);
        }
        if (LOWORD(wp) == LIMIT_ID && HIWORD(wp) == CBN_SELENDOK) {
            int i = (int)SendMessageA(s_limit, CB_GETCURSEL, 0, 0);
            if (i >= 0 && i < 7) {
                s_cfg.memory.limit_mb_s = s_limits[i];
                printf("[memory] limit %.2f MB/s (0=unlimited)\n", s_cfg.memory.limit_mb_s);
            }
            SetFocus(hwnd);
        }
        return 0;
    case WM_KEYDOWN: if (wp == VK_ESCAPE) s_running = 0; return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}
static void poll_messages(void)
{
    MSG msg;
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) { s_running = 0; break; }
        TranslateMessage(&msg); DispatchMessageA(&msg);
    }
}
static void select_limit(void)
{
    int selected = 0;
    SendMessageA(s_limit, CB_RESETCONTENT, 0, 0);
    for (int i = 0; i < 7; ++i) {
        SendMessageA(s_limit, CB_ADDSTRING, 0, (LPARAM)s_limit_names[i]);
        if (s_limits[i] == s_cfg.memory.limit_mb_s) selected = i;
    }
    if (selected == 0 && s_cfg.memory.limit_mb_s > 0) {
        char label[80]; snprintf(label, sizeof(label), "SDRAM: %.2f MB/s", s_cfg.memory.limit_mb_s);
        selected = (int)SendMessageA(s_limit, CB_ADDSTRING, 0, (LPARAM)label);
    }
    SendMessageA(s_limit, CB_SETCURSEL, selected, 0);
}
/* A candidate has its own framebuffer. Commit only after Attach succeeds. */
static int switch_project(int index)
{
    FvAdapter candidate = {0}; FvConfig cfg, old_cfg = s_cfg;
    Surface fresh, old_surface = s_surface; FvHost old_host = s_host;
    char err[1024] = "Unable to allocate framebuffer";
    if (index < 0 || index >= s_project_count || index == s_current) return 0;
    Fv_ReadConfig(s_folders[index], &cfg);
    if (Fv_LoadAdapter(s_folders[index], &cfg, &candidate, err, sizeof(err)) != 0) goto fail;
    if (create_surface(&fresh, cfg.width, cfg.height) != 0) goto unload;
    s_surface = fresh; s_cfg = cfg; bind_surface(); s_text_count = 0;
    if (candidate.Attach(&s_host) != 0) {
        strcpy(err, "Pv_Attach rejected the project; previous project retained.");
        destroy_surface(&s_surface); s_surface = old_surface; s_host = old_host; s_cfg = old_cfg;
        s_text_count = 0; goto unload;
    }
    if (s_ad->dll) FreeLibrary((HMODULE)s_ad->dll);
    *s_ad = candidate; destroy_surface(&old_surface);
    refresh_sensors(1);
    printf("[project] switched to %s (%dx%d @%dfps)\n", s_folders[index], cfg.width, cfg.height, cfg.fps);
    strcpy(s_folder, s_folders[index]); s_current = index;
    s_tx = s_ty = 0; s_paused = 0; s_fps_now = 0; s_underrun_frames = 0;
    SendMessageA(s_projects, CB_SETCURSEL, index, 0); select_limit(); timer_init();
    return 0;
unload:
    if (candidate.dll) FreeLibrary((HMODULE)candidate.dll);
fail:
    SendMessageA(s_projects, CB_SETCURSEL, s_current, 0);
    if (s_hwnd) MessageBoxA(s_hwnd, err, "Project switch failed", MB_OK | MB_ICONERROR);
    else fprintf(stderr, "Project switch failed: %s\n", err);
    timer_init(); return -1;
}
int Fv_RunHeadless(const FvConfig *cfg, FvAdapter *ad, const char *path)
{
    int result;
    s_cfg = *cfg; s_text_count = 0; s_underrun_frames = 0;
    if (create_surface(&s_surface, cfg->width, cfg->height) != 0) return 1;
    bind_surface();
    if (ad->Attach(&s_host) != 0) { destroy_surface(&s_surface); return 1; }
    ad->Update(0, 0, 1.0f / cfg->fps); ad->Render(); compose(cfg->fps);
    result = export_bmp(path);
    printf("memory: scan %.3f + estimated write %.3f = %.3f MB/s; limit %.3f; missing %zu pixels\n",
        s_stats.read_mb_s, s_stats.write_mb_s, s_stats.demand_mb_s, cfg->memory.limit_mb_s, s_stats.missing_pixels);
    destroy_surface(&s_surface); return result == 0 ? 0 : 1;
}
int Fv_RunHost(const char *folder, const FvConfig *cfg, FvAdapter *ad)
{
    WNDCLASSEXA wc = {0}; RECT rc = {0, 0, cfg->width, cfg->height + TOP + BOTTOM};
    HINSTANCE inst = GetModuleHandleA(NULL); int result = 0;
    INITCOMMONCONTROLSEX controls = {sizeof(controls), ICC_BAR_CLASSES};
    InitCommonControlsEx(&controls);
    rc.right = 1280; rc.bottom = 850;
    s_cfg = *cfg; s_ad = ad; s_current = Fv_AddProject(folder);
    strcpy(s_folder, s_folders[s_current]); s_running = 1;
    if (create_surface(&s_surface, cfg->width, cfg->height) != 0) return 1;
    bind_surface();
    wc.cbSize = sizeof(wc); wc.lpfnWndProc = wnd_proc; wc.hInstance = inst;
    wc.hCursor = LoadCursorA(NULL, (LPCSTR)IDC_ARROW); wc.lpszClassName = "EmbeddedScreenPreviewHost";
    if (!RegisterClassExA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) { result = 1; goto done; }
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, FALSE);
    s_hwnd = CreateWindowExA(0, wc.lpszClassName, "EmbeddedScreenPreview",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right-rc.left, rc.bottom-rc.top, NULL, NULL, inst, NULL);
    if (!s_hwnd) { result = 1; goto done; }
    s_projects = CreateWindowExA(0, "COMBOBOX", "", WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST,
        0, 0, 100, 300, s_hwnd, (HMENU)PROJECT_ID, inst, NULL);
    s_add = CreateWindowExA(0, "BUTTON", "Add project...", WS_CHILD | WS_VISIBLE,
        0, 0, 100, 26, s_hwnd, (HMENU)ADD_ID, inst, NULL);
    s_limit = CreateWindowExA(0, "COMBOBOX", "", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
        0, 0, 180, 250, s_hwnd, (HMENU)LIMIT_ID, inst, NULL);
    s_status = CreateWindowExA(0, "STATIC", "", WS_CHILD | WS_VISIBLE,
        0, 0, 100, 42, s_hwnd, NULL, inst, NULL);
    if (!s_projects || !s_add || !s_limit || !s_status) { result = 1; goto done; }
    for (int i = 0; i < s_project_count; ++i) SendMessageA(s_projects, CB_ADDSTRING, 0, (LPARAM)s_folders[i]);
    s_terminal_title = CreateWindowExW(0, L"STATIC", L"Terminal - host / adapter stdout and stderr", WS_CHILD | WS_VISIBLE,
        0, 0, 100, 22, s_hwnd, NULL, inst, NULL);
    s_terminal = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
        0, 0, 100, 100, s_hwnd, NULL, inst, NULL);
    s_sensor_title = CreateWindowExW(0, L"STATIC", L"Sensors", WS_CHILD | WS_VISIBLE, 0, 0, 100, 26, s_hwnd, NULL, inst, NULL);
    s_sensor_help = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 0, 0, 100, 60, s_hwnd, NULL, inst, NULL);
    for (int i = 0; i < 3; ++i) {
        s_sensor_labels[i] = CreateWindowExA(0, "STATIC", "", WS_CHILD | WS_VISIBLE, 0, 0, 100, 24, s_hwnd, NULL, inst, NULL);
        s_sliders[i] = CreateWindowExA(0, TRACKBAR_CLASSA, "", WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_AUTOTICKS,
            0, 0, 100, 36, s_hwnd, (HMENU)(INT_PTR)(SENSOR_BASE + i), inst, NULL);
        SendMessageA(s_sliders[i], TBM_SETRANGEMIN, FALSE, s_sensor_min[i]);
        SendMessageA(s_sliders[i], TBM_SETRANGEMAX, TRUE, s_sensor_max[i]);
    }
    s_reset = CreateWindowExW(0, L"BUTTON", L"Reset sensors", WS_CHILD | WS_VISIBLE, 0, 0, 100, 30, s_hwnd, (HMENU)RESET_ID, inst, NULL);
    /* Standard UI font avoids the scaled bitmap system font on high-DPI screens. */
    for (HWND child = GetWindow(s_hwnd, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
        SendMessageW(child, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    SendMessageA(s_projects, CB_SETCURSEL, s_current, 0); select_limit(); layout(s_hwnd);
    if (Fv_TerminalStart(s_terminal) != 0) SetWindowTextW(s_terminal, L"Unable to open a session log. stdout/stderr remain in the launching terminal.");
    printf("[host] Project: %s\n", s_folder);
    if (ad->Attach(&s_host) != 0) { result = 1; goto done; }
    refresh_sensors(1); Fv_TerminalPump();
    ShowWindow(s_hwnd, SW_SHOW); SetFocus(s_hwnd); timer_init();
    while (s_running) {
        LARGE_INTEGER t0, t1; float dt; double ms; char status[512], title[512]; const char *name;
        QueryPerformanceCounter(&t0); dt = timer_dt();
        poll_messages(); if (!s_running) break;
        if (s_pending >= 0) { int i = s_pending; s_pending = -1; switch_project(i); dt = 1.0f / s_cfg.fps; }
        poll_input(dt); if (!s_running) break;
        if (s_want_reload) {
            char err[1024]; s_want_reload = 0;
            if (ad->dll) FreeLibrary((HMODULE)ad->dll);
            memset(ad, 0, sizeof(*ad));
            s_text_count = 0;
            if (Fv_LoadAdapter(s_folder, &s_cfg, ad, err, sizeof(err)) != 0 || ad->Attach(&s_host) != 0) {
                MessageBoxA(s_hwnd, "Adapter reload failed. Restart after rebuilding.", "Reload failed", MB_OK | MB_ICONERROR);
                result = 1; break;
            }
            refresh_sensors(1); printf("[host] Adapter reloaded\n"); timer_init(); dt = 1.0f / s_cfg.fps;
        }
        if (!s_paused) ad->Update(s_tx, s_ty, dt);
        ad->Render();
        if (s_debug) {
            char debug[256]; snprintf(debug, sizeof(debug), "%.0f fps %.1f ms tilt %.0f,%.0f%s", s_fps_now, s_last_ms, s_tx, s_ty, s_paused ? " PAUSED" : "");
            Fv_HostDrawText(debug, 10, s_surface.h - 32, 0xffe0);
        }
        /* Render still executes while paused, so its estimated writes still count. */
        compose(s_fps_now > 0 ? s_fps_now : s_cfg.fps);
        snprintf(status, sizeof(status),
            "RGB565 %dx%d | scan %.1f Hz: %.2f MB/s | estimated writes: %.2f MB/s | total: %.2f MB/s\r\n"
            "Delivered: %.2f MB/s | %s | missing: %.1f%% | underrun frames: %lu | Space pause / F1 debug / F12 capture",
            s_surface.w, s_surface.h, s_cfg.memory.scan_hz, s_stats.read_mb_s, s_stats.write_mb_s, s_stats.demand_mb_s,
            s_stats.delivered_mb_s, s_stats.underrun ? "UNDERRUN" : "OK", 100.0 * s_stats.missing_pixels / ((double)s_surface.w * s_surface.h), s_underrun_frames);
        SetWindowTextA(s_status, status);
        Fv_TerminalPump();
        if (s_want_shot) { s_want_shot = 0; take_screenshot(); }
        InvalidateRect(s_hwnd, NULL, FALSE); UpdateWindow(s_hwnd);
        name = ad->Title ? ad->Title() : NULL;
        snprintf(title, sizeof(title), "%s [%.0f fps]%s", name ? name : (s_cfg.title[0] ? s_cfg.title : "EmbeddedScreenPreview"), s_fps_now, s_paused ? " PAUSED" : "");
        SetWindowTextA(s_hwnd, title);
        QueryPerformanceCounter(&t1); ms = (double)(t1.QuadPart-t0.QuadPart)*1000 / s_freq.QuadPart;
        s_last_ms = ms;
        if (ms < 1000.0/s_cfg.fps) Sleep((DWORD)(1000.0/s_cfg.fps-ms));
        QueryPerformanceCounter(&t1);
        ms = (double)(t1.QuadPart-t0.QuadPart)/s_freq.QuadPart;
        if (ms > 0) s_fps_now = s_fps_now > 0 ? s_fps_now*.9 + .1/ms : 1/ms;
    }
done:
    printf("[host] Session closed\n"); Fv_TerminalStop();
    if (s_hwnd && IsWindow(s_hwnd)) DestroyWindow(s_hwnd);
    s_hwnd = s_projects = s_add = s_limit = s_status = NULL;
    destroy_surface(&s_surface); s_ad = NULL;
    return result;
}
