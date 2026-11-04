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

#define TOP 142
#define BOTTOM 64
#define GUTTER 24
#define FIELD_BG RGB(230, 237, 239)
#define CARD_BG RGB(248, 251, 251)
#define INK RGB(22, 43, 52)
#define MUTED RGB(68, 92, 102)
#define ACCENT RGB(0, 99, 97)
#define WARNING RGB(128, 69, 12)
#define PROJECT_ID 101
#define ADD_ID 102
#define LIMIT_ID 103
#define MAX_PROJECTS 64
#define PANEL 270
#define SENSOR_BASE 201
#define RESET_ID 210
#define ADD_SENSOR_ID 211
#define EXTRA_SENSOR_ID 212
#define ACTION_RELOAD 301
#define ACTION_PAUSE 302
#define ACTION_DEBUG 303
#define ACTION_CAPTURE 304
#define ACTION_FULLSCREEN 306
#define ACTION_STEP 307
#define LANGUAGE_ID 308
#define SERIAL_SEND_ID 309
#define OPEN_DEMO_ID 213

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
typedef struct {
    UINT id;
    const wchar_t *caption, *english;
    int width;
    HWND window;
} ActionButton;
static ActionButton s_actions[] = {
    {ACTION_RELOAD, L"重载项目", L"Reload", 84, NULL},
    {ACTION_PAUSE, L"暂停模拟", L"Pause", 72, NULL},
    {ACTION_STEP, L"单帧", L"Step frame", 80, NULL},
    {ACTION_DEBUG, L"调试信息", L"Debug", 68, NULL},
    {ACTION_CAPTURE, L"截图", L"Capture", 65, NULL},
    {ACTION_FULLSCREEN, L"全屏", L"Fullscreen", 87, NULL}
};
#define ACTION_COUNT (sizeof(s_actions) / sizeof(s_actions[0]))
static HWND s_hwnd, s_projects, s_add, s_limit, s_status;
static HFONT s_ui_font, s_heading_font, s_title_font, s_log_font;
static HBRUSH s_card_brush, s_log_brush;
static int s_english, s_step_pending;
#define TR(zh, en) (s_english ? (en) : (zh))
static HWND s_language, s_serial_input, s_serial_eol, s_serial_send;
static HWND s_terminal, s_terminal_title, s_sensor_title, s_sensor_help, s_reset;
static HWND s_sensor_labels[3], s_sliders[3];
static HWND s_add_sensor, s_extra_picker, s_extra_label, s_extra_slider, s_open_demo;
typedef struct {
    int id;
    const wchar_t *name, *english, *unit;
    int minimum, maximum, initial;
    int added, value;
} ExtraSensor;
static ExtraSensor s_extra[] = {
    {FV_SENSOR_PRESSURE, L"气压", L"Pressure", L"hPa", 300, 1100, 1013, 0, 1013},
    {FV_SENSOR_DISTANCE, L"距离", L"Distance", L"cm", 2, 400, 50, 0, 50},
    {FV_SENSOR_VOLTAGE, L"电压", L"Voltage", L"mV", 0, 5000, 3300, 0, 3300},
    {FV_SENSOR_POTENTIOMETER, L"电位器", L"Potentiometer", L"ADC", 0, 4095, 2048, 0, 2048}
};
#define EXTRA_COUNT (sizeof(s_extra) / sizeof(s_extra[0]))
static ExtraSensor *s_active_extra;
static uint32_t s_led_colors[FV_LED_COUNT];
static int s_led_connected;
static const wchar_t *s_sensor_names[3] = {L"温度", L"湿度", L"光照"};
static const wchar_t *s_sensor_en[3] = {L"Temperature", L"Humidity", L"Light"};
static const wchar_t *s_sensor_units[3] = {L"°C", L"%", L"lux"};
static const int s_sensor_min[3] = {-40, 0, 0};
static const int s_sensor_max[3] = {125, 100, 10000};
static const int s_sensor_default[3] = {25, 50, 500};
static int s_sensor_values[3] = {25, 50, 500};
static int s_touching;
static FvAdapter *s_ad;
static void refresh_sensors(int reset);
static void select_limit(void);
static void layout(HWND hwnd);
static FvViewport preview_viewport(void);
static FvHost s_host;
static FvConfig s_cfg;
static char s_folder[MAX_PATH];
static char s_folders[MAX_PROJECTS][MAX_PATH];
static int s_project_count, s_current, s_pending = -1;
static int s_running = 1, s_paused, s_debug, s_fullscreen, s_want_reload, s_want_shot;
static float s_tx, s_ty;
static float s_tilt_max = 6300, s_tilt_ramp = 18000, s_tilt_decay = 24000;
static int s_k1_last, s_k2_last, s_shot_no;
static double s_fps_now, s_last_ms;
static LARGE_INTEGER s_freq, s_last;
static FvMemoryStats s_stats;
static unsigned long s_underrun_frames;
static const double s_limits[] = {0, 120, 80, 60, 40, 20, 5};
static const wchar_t *s_limit_names[] = {L"不限速", L"120 MB/s", L"80 MB/s", L"60 MB/s", L"40 MB/s", L"20 MB/s", L"5 MB/s"};
#define LIMIT_COUNT (sizeof(s_limits) / sizeof(s_limits[0]))

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

static void toggle_fullscreen(void)
{
    static WINDOWPLACEMENT placement = {.length = sizeof(placement)};
    LONG style = GetWindowLongA(s_hwnd, GWL_STYLE);
    if (!s_fullscreen) {
        MONITORINFO monitor = {.cbSize = sizeof(monitor)};
        HMONITOR handle = MonitorFromWindow(s_hwnd, MONITOR_DEFAULTTONEAREST);
        if (!GetWindowPlacement(s_hwnd, &placement) || !GetMonitorInfoA(handle, &monitor)) return;
        RECT r = monitor.rcMonitor;
        SetWindowLongA(s_hwnd, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
        SetWindowPos(s_hwnd, HWND_TOP, r.left, r.top, r.right - r.left, r.bottom - r.top,
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    } else {
        SetWindowLongA(s_hwnd, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(s_hwnd, &placement);
        SetWindowPos(s_hwnd, NULL, 0, 0, 0, 0,
                     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
    }
    s_fullscreen = !s_fullscreen;
}

static void sync_action_buttons(void)
{
    for (size_t i = 0; i < ACTION_COUNT; ++i) {
        ActionButton *action = &s_actions[i];
        if (!action->window) continue;
        const wchar_t *caption = TR(action->caption, action->english);
        if (action->id == ACTION_PAUSE && s_paused) caption = TR(L"继续模拟", L"Resume");
        if (action->id == ACTION_DEBUG && s_debug) caption = TR(L"隐藏调试", L"Hide debug");
        if (action->id == ACTION_FULLSCREEN && s_fullscreen) caption = TR(L"退出全屏", L"Exit full");
        wchar_t previous[32];
        GetWindowTextW(action->window, previous, 32);
        if (wcscmp(previous, caption) != 0) {
            SetWindowTextW(action->window, caption);
            InvalidateRect(action->window, NULL, FALSE);
        }
    }
}

static void run_action(UINT id)
{
    switch (id) {
    case ACTION_RELOAD: s_want_reload = 1; break;
    case ACTION_PAUSE: s_paused = !s_paused; s_step_pending = 0; break;
    case ACTION_STEP: s_paused = 1; if (s_step_pending < 32) ++s_step_pending; break;
    case ACTION_DEBUG: s_debug = !s_debug; break;
    case ACTION_CAPTURE: s_want_shot = 1; break;
    case ACTION_FULLSCREEN: toggle_fullscreen(); break;
    }
    sync_action_buttons();
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

        if (f1  && !l_f1)  run_action(ACTION_DEBUG);
        if (sp  && !l_sp)  run_action(ACTION_PAUSE);
        if (f12 && !l_f12) run_action(ACTION_CAPTURE);
        if (rr  && !l_r)   run_action(ACTION_RELOAD);

        if (f11 && !l_f11) run_action(ACTION_FULLSCREEN);

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
typedef struct {
    RECT client, display, log, inputs, outputs;
} Dashboard;

static int has_sensor_inputs(void)
{
    return s_ad && s_ad->SetSensor;
}

static Dashboard dashboard(void)
{
    Dashboard d = {0};
    GetClientRect(s_hwnd, &d.client);
    int log_height = d.client.bottom * 22 / 100;
    if (log_height < 164) log_height = 164;
    if (log_height > 220) log_height = 220;
    int log_top = d.client.bottom - log_height;
    int right = d.client.right - PANEL - GUTTER;
    int output_top = !has_sensor_inputs() ? 235 : s_active_extra ? 536 : 426;
    int panel_bottom = d.client.bottom - GUTTER;
    d.display = (RECT){GUTTER + 10, TOP, right - GUTTER - 10, log_top - BOTTOM};
    d.log = (RECT){GUTTER, log_top, right - GUTTER, panel_bottom};
    d.inputs = (RECT){right, 105, right + PANEL, output_top - 12};
    d.outputs = (RECT){right, output_top, right + PANEL, output_top + 146};
    return d;
}

static FvViewport fitted_display(RECT area)
{
    FvViewport v = Fv_FitViewport(area.right - area.left, area.bottom - area.top,
        0, 0, s_surface.w, s_surface.h);
    v.x += area.left;
    v.y += area.top;
    return v;
}

static FvViewport preview_viewport(void)
{
    return fitted_display(dashboard().display);
}

static int can_add_sensor(void)
{
    if (!has_sensor_inputs()) return 0;
    for (size_t i = 0; i < EXTRA_COUNT; ++i)
        if (!s_extra[i].added) return 1;
    return 0;
}

static void update_extra_readout(void)
{
    if (!s_active_extra) return;
    wchar_t text[96];
    swprintf(text, 96, L"%ls  %d %ls", TR(s_active_extra->name, s_active_extra->english), s_active_extra->value, s_active_extra->unit);
    SetWindowTextW(s_extra_label, text);
}

static void show_extra_sensor(void)
{
    int visible = has_sensor_inputs() && s_active_extra;
    EnableWindow(s_add_sensor, can_add_sensor());
    ShowWindow(s_extra_picker, visible ? SW_SHOWNA : SW_HIDE);
    ShowWindow(s_extra_label, visible ? SW_SHOWNA : SW_HIDE);
    ShowWindow(s_extra_slider, visible ? SW_SHOWNA : SW_HIDE);
    if (!visible) return;
    update_extra_readout();
    SendMessageA(s_extra_slider, TBM_SETPOS, TRUE, s_active_extra->value);
}

static void select_extra_sensor(int row)
{
    LRESULT index = SendMessageA(s_extra_picker, CB_GETITEMDATA, row, 0);
    if (index == CB_ERR || (size_t)index >= EXTRA_COUNT) return;
    s_active_extra = &s_extra[index];
    SendMessageA(s_extra_slider, TBM_SETRANGEMIN, FALSE, s_active_extra->minimum);
    SendMessageA(s_extra_slider, TBM_SETRANGEMAX, FALSE, s_active_extra->maximum);
    SendMessageA(s_extra_slider, TBM_SETTICFREQ,
        (s_active_extra->maximum - s_active_extra->minimum) / 5, 0);
    show_extra_sensor();
}

static void add_extra_sensor(void)
{
    if (!can_add_sensor()) return;
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    for (size_t i = 0; i < EXTRA_COUNT; ++i) {
        AppendMenuW(menu, MF_STRING | (s_extra[i].added ? MF_GRAYED : 0), i + 1, TR(s_extra[i].name, s_extra[i].english));
    }
    RECT button;
    GetWindowRect(s_add_sensor, &button);
    UINT selection = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY,
        button.left, button.bottom, 0, s_hwnd, NULL);
    DestroyMenu(menu);
    if (!selection || selection > EXTRA_COUNT) return;
    ExtraSensor *sensor = &s_extra[selection - 1];
    LRESULT row = SendMessageW(s_extra_picker, CB_ADDSTRING, 0, (LPARAM)TR(sensor->name, sensor->english));
    if (row < 0) return;
    SendMessageA(s_extra_picker, CB_SETITEMDATA, row, selection - 1);
    SendMessageA(s_extra_picker, CB_SETCURSEL, row, 0);
    sensor->added = 1;
    select_extra_sensor((int)row);
    layout(s_hwnd);
    s_ad->SetSensor(sensor->id, (float)sensor->value);
    printf("[传感器] 已添加编号 %d\n", sensor->id);
}

static void update_base_readout(int index)
{
    if (!s_sensor_labels[index]) return;
    wchar_t text[96];
    swprintf(text, 96, L"%ls  %d %ls", TR(s_sensor_names[index], s_sensor_en[index]), s_sensor_values[index], s_sensor_units[index]);
    SetWindowTextW(s_sensor_labels[index], text);
}

static void refresh_sensors(int reset)
{
    int supported = has_sensor_inputs();
    int visibility = supported ? SW_SHOWNA : SW_HIDE;
    if (s_sensor_help) SetWindowTextW(s_sensor_help, supported ?
        TR(L"调整输入，查看 LCD、指示灯和日志。", L"Adjust inputs and watch the preview.") :
        TR(L"当前项目未接入传感器。可打开 Demo 体验外设。", L"No sensor inputs. Open Demo to try them."));
    for (int i = 0; i < 3; ++i) {
        if (reset) s_sensor_values[i] = s_sensor_default[i];
        if (s_sliders[i]) {
            SendMessageA(s_sliders[i], TBM_SETPOS, TRUE, s_sensor_values[i]);
            ShowWindow(s_sliders[i], visibility);
            ShowWindow(s_sensor_labels[i], visibility);
        }
        update_base_readout(i);
        if (supported) s_ad->SetSensor(i + 1, (float)s_sensor_values[i]);
    }
    for (size_t i = 0; i < EXTRA_COUNT; ++i) {
        if (reset) s_extra[i].value = s_extra[i].initial;
        if (supported && s_extra[i].added) s_ad->SetSensor(s_extra[i].id, (float)s_extra[i].value);
    }
    if (s_add_sensor) {
        ShowWindow(s_add_sensor, visibility);
        ShowWindow(s_reset, visibility);
        ShowWindow(s_open_demo, supported ? SW_HIDE : SW_SHOWNA);
        show_extra_sensor();
        layout(s_hwnd);
    }
}

static void layout(HWND hwnd)
{
    Dashboard d = dashboard();
    int right = d.inputs.left;
    int left_width = d.log.right - d.log.left;
    int log_y = d.log.top;
    int reload_x = d.inputs.right - s_actions[0].width;
    int add_x = reload_x - 8 - 110;
    MoveWindow(s_projects, GUTTER, 67, add_x - GUTTER - 8, 340, FALSE);
    MoveWindow(s_add, add_x, 65, 110, 30, FALSE);
    MoveWindow(s_actions[0].window, reload_x, 65, s_actions[0].width, 30, FALSE);
    int action_x = d.log.right - 12;
    for (size_t i = ACTION_COUNT; i-- > 1;) {
        action_x -= s_actions[i].width;
        MoveWindow(s_actions[i].window, action_x, 111, s_actions[i].width, 25, FALSE);
        action_x -= 6;
    }
    const int limit_width = 176;
    MoveWindow(s_limit, d.log.right - 16 - limit_width, d.display.bottom + 12, limit_width, 260, FALSE);
    MoveWindow(s_status, GUTTER + 16, d.display.bottom + 9, left_width - 42 - limit_width, 39, FALSE);
    MoveWindow(s_terminal_title, GUTTER + 16, log_y + 12, left_width - 32, 24, FALSE);
    int input_y = d.log.bottom - 40;
    MoveWindow(s_terminal, GUTTER + 16, log_y + 40, left_width - 32, input_y - log_y - 48, FALSE);
    MoveWindow(s_serial_input, GUTTER + 16, input_y, left_width - 208, 27, FALSE);
    MoveWindow(s_serial_eol, d.log.right - 184, input_y, 92, 180, FALSE);
    MoveWindow(s_serial_send, d.log.right - 84, input_y, 68, 27, FALSE);
    MoveWindow(s_language, d.inputs.right - 96, 20, 96, 29, FALSE);
    MoveWindow(s_sensor_title, right + 16, 117, PANEL - 32, 28, FALSE);
    MoveWindow(s_sensor_help, right + 16, 150, PANEL - 32, has_sensor_inputs() ? 30 : 42, FALSE);
    for (int i = 0; i < 3; ++i) {
        MoveWindow(s_sensor_labels[i], right + 16, 211 + i * 52, PANEL - 32, 20, FALSE);
        MoveWindow(s_sliders[i], right + 16, 231 + i * 52, PANEL - 32, 25, FALSE);
    }
    MoveWindow(s_add_sensor, right + 16, 376, 126, 30, FALSE);
    MoveWindow(s_reset, right + 152, 376, PANEL - 168, 30, FALSE);
    MoveWindow(s_extra_picker, right + 16, 440, PANEL - 32, 190, FALSE);
    MoveWindow(s_extra_label, right + 16, 470, PANEL - 32, 20, FALSE);
    MoveWindow(s_extra_slider, right + 16, 491, PANEL - 32, 25, FALSE);
    MoveWindow(s_open_demo, right + 16, 190, PANEL - 32, 28, FALSE);
    RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_NOERASE);
}
static void panel(HDC dc, RECT r, COLORREF fill, COLORREF edge, int radius)
{
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, edge);
    HGDIOBJ old_brush = SelectObject(dc, brush);
    HGDIOBJ old_pen = SelectObject(dc, pen);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
    DeleteObject(pen);
    DeleteObject(brush);
}
static void label(HDC dc, HFONT font, COLORREF color, RECT rect, const wchar_t *text, UINT flags)
{
    HGDIOBJ previous = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, text, -1, &rect, flags | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    SelectObject(dc, previous);
}
static void paint_leds(HDC dc, RECT region)
{
    int right = region.left, top = region.top;
    RECT content = {right + 16, top + 10, right + PANEL - 16, top + 36};
    label(dc, s_heading_font, INK, content, TR(L"RGB 指示灯", L"RGB indicators"), DT_LEFT);
    if (!s_led_connected) {
        label(dc, s_ui_font, MUTED, (RECT){right + 16, top + 42, right + PANEL - 16, top + 65},
              TR(L"当前项目未接入灯光输出", L"No LED output from this project"), DT_LEFT);
        return;
    }
    for (int i = 0; i < FV_LED_COUNT; ++i) {
        int center = right + 54 + i * 81;
        uint32_t color = s_led_colors[i];
        COLORREF fill = color ? RGB((color >> 16) & 255, (color >> 8) & 255, color & 255) : RGB(52, 68, 74);
        RECT bezel = {center - 18, top + 46, center + 18, top + 82};
        RECT lens = {center - 13, top + 51, center + 13, top + 77};
        panel(dc, bezel, RGB(212, 225, 228), RGB(160, 179, 185), 36);
        panel(dc, lens, fill, RGB(44, 62, 69), 26);
        wchar_t value[16];
        if (color) swprintf(value, 16, L"#%06lX", (unsigned long)color);
        else wcscpy(value, TR(L"熄灭", L"Off"));
        wchar_t name[] = L"LED 1";
        name[4] = (wchar_t)(L'1' + i);
        label(dc, s_ui_font, INK, (RECT){center - 40, top + 88, center + 40, top + 107}, name, DT_CENTER);
        label(dc, s_log_font, MUTED, (RECT){center - 40, top + 109, center + 40, top + 127}, value, DT_CENTER);
    }
}

static void refresh_leds(void)
{
    int connected = s_ad && s_ad->LedColor;
    int changed = connected != s_led_connected;
    s_led_connected = connected;
    for (unsigned i = 0; i < FV_LED_COUNT; ++i) {
        uint32_t color = connected ? s_ad->LedColor(i) & 0xffffffu : 0;
        if (color != s_led_colors[i]) changed = 1;
        s_led_colors[i] = color;
    }
    if (changed && s_hwnd) {
        RECT region = dashboard().outputs;
        InvalidateRect(s_hwnd, &region, FALSE);
    }
}

static void paint_lcd(HDC dc, FvViewport v)
{
    if (!s_surface.dc || v.w <= 0 || v.h <= 0) return;
    SetStretchBltMode(dc, COLORONCOLOR);
    StretchBlt(dc, v.x, v.y, v.w, v.h, s_surface.dc, 0, 0,
               s_surface.w, s_surface.h, SRCCOPY);
}
static void paint(HDC dc)
{
    Dashboard d = dashboard();
    int right = d.inputs.left;
    int left_edge = d.log.right;
    COLORREF border = RGB(181, 199, 205);
    HBRUSH background = CreateSolidBrush(FIELD_BG);
    FillRect(dc, &d.client, background);
    DeleteObject(background);
    panel(dc, (RECT){GUTTER, 105, left_edge, d.log.top - 8}, CARD_BG, border, 14);
    panel(dc, d.log, CARD_BG, border, 14);
    panel(dc, d.inputs, CARD_BG, border, 14);
    RECT outputs = d.outputs;
    if (!s_led_connected) outputs.bottom = outputs.top + 76;
    panel(dc, outputs, CARD_BG, border, 14);
    panel(dc, d.display, RGB(14, 26, 32), RGB(14, 26, 32), 8);
    if (has_sensor_inputs()) {
        label(dc, s_ui_font, MUTED, (RECT){right + 16, 183, right + PANEL - 16, 206}, TR(L"基础输入", L"Built-in inputs"), DT_LEFT);
        if (s_active_extra)
            label(dc, s_ui_font, MUTED, (RECT){right + 16, 411, right + PANEL - 16, 435}, TR(L"扩展输入", L"Added inputs"), DT_LEFT);
    }
    label(dc, s_title_font, INK, (RECT){GUTTER, 14, right, 51}, TR(L"嵌入式模拟器", L"Embedded Workbench"), DT_LEFT);
    label(dc, s_heading_font, INK, (RECT){GUTTER + 16, 110, left_edge - 16, 138}, TR(L"LCD 预览", L"LCD preview"), DT_LEFT);
    label(dc, s_ui_font, s_stats.underrun ? WARNING : ACCENT,
          (RECT){GUTTER + 123, 110, GUTTER + 210, 138},
          s_stats.underrun ? TR(L"带宽不足", L"Bandwidth low") : TR(L"扫描正常", L"Scanout OK"), DT_LEFT);
    paint_leds(dc, d.outputs);
    paint_lcd(dc, fitted_display(d.display));
}

static void paint_update(HDC dc, RECT dirty)
{
    FvViewport v = preview_viewport();
    int width = dirty.right - dirty.left, height = dirty.bottom - dirty.top;
    if (width <= 0 || height <= 0) return;
    /* A video frame already lives in a DIB. Copy it directly without erasing. */
    if (s_surface.dc && dirty.left >= v.x && dirty.top >= v.y &&
        dirty.right <= v.x + v.w && dirty.bottom <= v.y + v.h) {
        paint_lcd(dc, v);
        return;
    }
    /* On resize or layout changes, compose the dirty area before presenting it. */
    HDC buffer = CreateCompatibleDC(dc);
    HBITMAP bitmap = buffer ? CreateCompatibleBitmap(dc, width, height) : NULL;
    if (bitmap) {
        HGDIOBJ previous = SelectObject(buffer, bitmap);
        SetViewportOrgEx(buffer, -dirty.left, -dirty.top, NULL);
        paint(buffer);
        BitBlt(dc, dirty.left, dirty.top, width, height, buffer, dirty.left, dirty.top, SRCCOPY);
        SelectObject(buffer, previous);
        DeleteObject(bitmap);
    } else {
        paint(dc);
    }
    if (buffer) DeleteDC(buffer);
}

static void paint_button(const DRAWITEMSTRUCT *item)
{
    wchar_t caption[80];
    RECT r = item->rcItem;
    int disabled = (item->itemState & ODS_DISABLED) != 0;
    int selected = (item->itemState & ODS_SELECTED) != 0;
    GetWindowTextW(item->hwndItem, caption, 80);
    int active = (item->CtlID == ACTION_PAUSE && s_paused) ||
        (item->CtlID == ACTION_DEBUG && s_debug) || (item->CtlID == ACTION_FULLSCREEN && s_fullscreen);
    if (item->CtlID == ADD_ID || item->CtlID == ACTION_RELOAD || item->CtlID == LANGUAGE_ID) {
        HBRUSH field = CreateSolidBrush(FIELD_BG);
        FillRect(item->hDC, &r, field);
        DeleteObject(field);
    } else {
        FillRect(item->hDC, &r, s_card_brush);
    }
    panel(item->hDC, r, disabled ? FIELD_BG : selected || active ? RGB(198, 222, 221) : RGB(218, 235, 234),
          RGB(166, 194, 195), 8);
    label(item->hDC, s_ui_font, disabled ? MUTED : ACCENT, r, caption, DT_CENTER);
    if (item->itemState & ODS_FOCUS) {
        InflateRect(&r, -4, -4);
        DrawFocusRect(item->hDC, &r);
    }
}
static int touch_position(LPARAM lp, int *x, int *y)
{
    FvViewport v = preview_viewport();
    return Fv_MapTouch(v, s_surface.w, s_surface.h, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), x, y);
}
static void pick_project(void)
{
    BROWSEINFOW bi = {0}; LPITEMIDLIST id; char folder[MAX_PATH];
    bi.hwndOwner = s_hwnd; bi.lpszTitle = TR(L"选择含有 preview.ini 或适配器 DLL 的项目文件夹", L"Choose a folder with preview.ini or an adapter DLL");
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    id = SHBrowseForFolderW(&bi);
    if (id) {
        if (SHGetPathFromIDListA(id, folder)) s_pending = Fv_AddProject(folder);
        CoTaskMemFree(id);
    }
    SetFocus(s_hwnd);
}
static void open_demo(void)
{
    char path[MAX_PATH];
    DWORD length = GetModuleFileNameA(NULL, path, MAX_PATH);
    if (!length || length >= MAX_PATH) return;
    char *slash = strrchr(path, '\\');
    if (!slash) return;
    *slash = 0;
    s_pending = Fv_AddProject(path);
}

static void set_bandwidth(size_t index)
{
    if (index >= LIMIT_COUNT) return;
    s_cfg.memory.limit_mb_s = s_limits[index];
    printf("[带宽] SDRAM 上限 %.2f MB/s（0 表示不限速）\n", s_cfg.memory.limit_mb_s);
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_DESTROY: s_running = 0; PostQuitMessage(0); return 0;
    case WM_GETMINMAXINFO:
        ((MINMAXINFO *)lp)->ptMinTrackSize.x = 980;
        ((MINMAXINFO *)lp)->ptMinTrackSize.y = 790; return 0;
    case WM_SIZE: if (s_projects) layout(hwnd); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_DRAWITEM: {
        const DRAWITEMSTRUCT *item = (const DRAWITEMSTRUCT *)lp;
        if (item->CtlID == ADD_ID || item->CtlID == RESET_ID || item->CtlID == ADD_SENSOR_ID || item->CtlID == OPEN_DEMO_ID || item->CtlID == LANGUAGE_ID || item->CtlID == SERIAL_SEND_ID ||
            (item->CtlID >= ACTION_RELOAD && item->CtlID <= ACTION_STEP)) { paint_button(item); return TRUE; }
        break;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)wp;
        HWND child = (HWND)lp;
        int terminal = child == s_terminal;
        SetTextColor(dc, terminal ? RGB(221, 238, 238) : child == s_sensor_help ? MUTED : INK);
        SetBkColor(dc, terminal ? RGB(22, 43, 52) : CARD_BG);
        return (LRESULT)(terminal ? s_log_brush : s_card_brush);
    }
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(hwnd, &ps); paint_update(dc, ps.rcPaint); EndPaint(hwnd, &ps); return 0;
    }
    case WM_HSCROLL:
        if ((HWND)lp == s_extra_slider && s_active_extra && s_ad && s_ad->SetSensor) {
            s_active_extra->value = (int)SendMessageA(s_extra_slider, TBM_GETPOS, 0, 0);
            s_ad->SetSensor(s_active_extra->id, (float)s_active_extra->value);
            update_extra_readout();
            return 0;
        }
        for (int i = 0; i < 3; ++i) if ((HWND)lp == s_sliders[i] && has_sensor_inputs()) {
            int value = (int)SendMessageA(s_sliders[i], TBM_GETPOS, 0, 0);
            if (value != s_sensor_values[i]) {
                s_sensor_values[i] = value;
                update_base_readout(i);
                s_ad->SetSensor(i + 1, (float)value);
            }
            break;
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
        if (LOWORD(wp) >= ACTION_RELOAD && LOWORD(wp) <= ACTION_STEP && HIWORD(wp) == BN_CLICKED) {
            run_action(LOWORD(wp));
            SetFocus(hwnd);
            return 0;
        }
        if (LOWORD(wp) == OPEN_DEMO_ID && HIWORD(wp) == BN_CLICKED) open_demo();
        if (LOWORD(wp) == ADD_SENSOR_ID && HIWORD(wp) == BN_CLICKED) add_extra_sensor();
        if (LOWORD(wp) == EXTRA_SENSOR_ID && HIWORD(wp) == CBN_SELENDOK)
            select_extra_sensor((int)SendMessageA(s_extra_picker, CB_GETCURSEL, 0, 0));
        if (LOWORD(wp) == RESET_ID && HIWORD(wp) == BN_CLICKED) { refresh_sensors(1); SetFocus(hwnd); }
        if (LOWORD(wp) == ADD_ID && HIWORD(wp) == BN_CLICKED) pick_project();
        if (LOWORD(wp) == PROJECT_ID && HIWORD(wp) == CBN_SELENDOK) {
            s_pending = (int)SendMessageA(s_projects, CB_GETCURSEL, 0, 0); SetFocus(hwnd);
        }
        if (LOWORD(wp) == LIMIT_ID && HIWORD(wp) == CBN_SELENDOK) {
            int i = (int)SendMessageA(s_limit, CB_GETCURSEL, 0, 0);
            if (i >= 0) set_bandwidth((size_t)i);
            SetFocus(hwnd);
        }
        return 0;
    case WM_KEYDOWN: if (wp == VK_ESCAPE) s_running = 0; return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
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
    for (size_t i = 0; i < LIMIT_COUNT; ++i) {
        wchar_t option[64];
        swprintf(option, 64, TR(L"SDRAM 上限  %ls", L"SDRAM cap  %ls"),
            i == 0 ? TR(s_limit_names[0], L"Unlimited") : s_limit_names[i]);
        SendMessageW(s_limit, CB_ADDSTRING, 0, (LPARAM)option);
        if (s_limits[i] == s_cfg.memory.limit_mb_s) selected = (int)i;
    }
    if (selected == 0 && s_cfg.memory.limit_mb_s > 0) {
        wchar_t option[80];
        swprintf(option, 80, TR(L"SDRAM 上限  %.2f MB/s", L"SDRAM cap  %.2f MB/s"), s_cfg.memory.limit_mb_s);
        selected = (int)SendMessageW(s_limit, CB_ADDSTRING, 0, (LPARAM)option);
    }
    SendMessageA(s_limit, CB_SETCURSEL, selected, 0);
}
/* A candidate has its own framebuffer. Commit only after Attach succeeds. */
static void path_for_log(const char *path, char output[MAX_PATH * 4])
{
    wchar_t wide[MAX_PATH];
    if (!MultiByteToWideChar(CP_ACP, 0, path, -1, wide, MAX_PATH) ||
        !WideCharToMultiByte(CP_UTF8, 0, wide, -1, output, MAX_PATH * 4, NULL, NULL))
        snprintf(output, MAX_PATH * 4, "%s", path);
}

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
    char log_path[MAX_PATH * 4];
    path_for_log(s_folders[index], log_path);
    printf("[项目] 切换到 %s (%dx%d @%dfps)\n", log_path, cfg.width, cfg.height, cfg.fps);
    strcpy(s_folder, s_folders[index]); s_current = index;
    s_tx = s_ty = 0; s_paused = 0; s_step_pending = 0; s_fps_now = 0; s_underrun_frames = 0;
    sync_action_buttons();
    SendMessageA(s_projects, CB_SETCURSEL, index, 0); select_limit(); timer_init();
    if (s_hwnd) InvalidateRect(s_hwnd, NULL, FALSE);
    return 0;
unload:
    if (candidate.dll) FreeLibrary((HMODULE)candidate.dll);
fail:
    SendMessageA(s_projects, CB_SETCURSEL, s_current, 0);
    if (s_hwnd) {
        wchar_t details[1024];
        if (!MultiByteToWideChar(CP_UTF8, 0, err, -1, details, 1024))
            wcscpy(details, L"无法加载适配器，请检查项目文件夹和构建日志。");
        MessageBoxW(s_hwnd, details, TR(L"项目切换失败", L"Project switch failed"), MB_OK | MB_ICONERROR);
    }
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
    WNDCLASSEXW wc = {0}; RECT rc = {0, 0, cfg->width, cfg->height + TOP + BOTTOM};
    HINSTANCE inst = GetModuleHandleA(NULL); int result = 0;
    INITCOMMONCONTROLSEX controls = {sizeof(controls), ICC_BAR_CLASSES};
    InitCommonControlsEx(&controls);
    rc.right = 1280; rc.bottom = 850;
    s_cfg = *cfg; s_ad = ad; s_current = Fv_AddProject(folder);
    strcpy(s_folder, s_folders[s_current]); s_running = 1;
    if (create_surface(&s_surface, cfg->width, cfg->height) != 0) return 1;
    s_card_brush = CreateSolidBrush(CARD_BG);
    s_log_brush = CreateSolidBrush(INK);
    s_ui_font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
        0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    s_heading_font = CreateFontW(-18, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET,
        0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    s_title_font = CreateFontW(-29, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET,
        0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    s_log_font = CreateFontW(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
        0, 0, CLEARTYPE_QUALITY, 0, L"Consolas");
    bind_surface();
    wc.cbSize = sizeof(wc); wc.lpfnWndProc = wnd_proc; wc.hInstance = inst;
    wc.hCursor = LoadCursorA(NULL, (LPCSTR)IDC_ARROW); wc.lpszClassName = L"EmbeddedScreenPreviewHost";
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) { result = 1; goto done; }
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, FALSE);
    s_hwnd = CreateWindowExW(0, wc.lpszClassName, TR(L"嵌入式模拟器", L"Embedded Workbench"),
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right-rc.left, rc.bottom-rc.top, NULL, NULL, inst, NULL);
    if (!s_hwnd) { result = 1; goto done; }
    SetWindowTextW(s_hwnd, TR(L"嵌入式模拟器", L"Embedded Workbench"));
    s_projects = CreateWindowExA(0, "COMBOBOX", "", WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST,
        0, 0, 100, 300, s_hwnd, (HMENU)PROJECT_ID, inst, NULL);
    s_add = CreateWindowExW(0, L"BUTTON", L"添加项目…", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 100, 26, s_hwnd, (HMENU)ADD_ID, inst, NULL);
    for (size_t i = 0; i < ACTION_COUNT; ++i) {
        ActionButton *action = &s_actions[i];
        action->window = CreateWindowExW(0, L"BUTTON", action->caption, WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, action->width, 28, s_hwnd, (HMENU)(UINT_PTR)action->id, inst, NULL);
        if (!action->window) { result = 1; goto done; }
    }
    s_limit = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
        0, 0, 180, 250, s_hwnd, (HMENU)LIMIT_ID, inst, NULL);
    s_status = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE,
        0, 0, 100, 42, s_hwnd, NULL, inst, NULL);
    if (!s_projects || !s_add || !s_limit || !s_status) { result = 1; goto done; }
    for (int i = 0; i < s_project_count; ++i) SendMessageA(s_projects, CB_ADDSTRING, 0, (LPARAM)s_folders[i]);
    s_terminal_title = CreateWindowExW(0, L"STATIC", L"会话日志", WS_CHILD | WS_VISIBLE,
        0, 0, 100, 22, s_hwnd, NULL, inst, NULL);
    s_terminal = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
        0, 0, 100, 100, s_hwnd, NULL, inst, NULL);
    s_sensor_title = CreateWindowExW(0, L"STATIC", L"传感器", WS_CHILD | WS_VISIBLE, 0, 0, 100, 26, s_hwnd, NULL, inst, NULL);
    s_sensor_help = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 0, 0, 100, 60, s_hwnd, NULL, inst, NULL);
    for (int i = 0; i < 3; ++i) {
        s_sensor_labels[i] = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 0, 0, 100, 24, s_hwnd, NULL, inst, NULL);
        s_sliders[i] = CreateWindowExA(0, TRACKBAR_CLASSA, "", WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_AUTOTICKS,
            0, 0, 100, 36, s_hwnd, (HMENU)(INT_PTR)(SENSOR_BASE + i), inst, NULL);
        SendMessageA(s_sliders[i], TBM_SETRANGEMIN, FALSE, s_sensor_min[i]);
        SendMessageA(s_sliders[i], TBM_SETRANGEMAX, TRUE, s_sensor_max[i]);
        SendMessageA(s_sliders[i], TBM_SETTICFREQ, (s_sensor_max[i] - s_sensor_min[i]) / 5, 0);
    }
    s_reset = CreateWindowExW(0, L"BUTTON", L"重置", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 100, 30, s_hwnd, (HMENU)RESET_ID, inst, NULL);
    s_add_sensor = CreateWindowExW(0, L"BUTTON", L"添加传感器…", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 100, 31, s_hwnd, (HMENU)ADD_SENSOR_ID, inst, NULL);
    s_extra_picker = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | CBS_DROPDOWNLIST,
        0, 0, 100, 190, s_hwnd, (HMENU)EXTRA_SENSOR_ID, inst, NULL);
    s_extra_label = CreateWindowExW(0, L"STATIC", L"", WS_CHILD,
        0, 0, 100, 22, s_hwnd, NULL, inst, NULL);
    s_extra_slider = CreateWindowExA(0, TRACKBAR_CLASSA, "", WS_CHILD | TBS_HORZ | TBS_AUTOTICKS,
        0, 0, 100, 30, s_hwnd, NULL, inst, NULL);
    s_open_demo = CreateWindowExW(0, L"BUTTON", L"打开 Demo", WS_CHILD | BS_OWNERDRAW,
        0, 0, 100, 28, s_hwnd, (HMENU)OPEN_DEMO_ID, inst, NULL);
    if (!s_open_demo || !s_add_sensor || !s_extra_picker || !s_extra_label || !s_extra_slider) { result = 1; goto done; }
    for (HWND child = GetWindow(s_hwnd, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        HFONT font = child == s_terminal ? s_log_font :
            child == s_sensor_title || child == s_terminal_title ? s_heading_font : s_ui_font;
        SendMessageW(child, WM_SETFONT, (WPARAM)font, TRUE);
    }
    SendMessageW(s_terminal, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(10, 10));
    SendMessageA(s_projects, CB_SETCURSEL, s_current, 0); select_limit(); layout(s_hwnd);
    if (Fv_TerminalStart(s_terminal) != 0) SetWindowTextW(s_terminal, TR(L"无法打开会话日志。输出仍显示在启动终端中。", L"Cannot open the log. Output remains in the launching terminal."));
    char log_path[MAX_PATH * 4];
    path_for_log(s_folder, log_path);
    printf("[项目] %s\n", log_path);
    if (ad->Attach(&s_host) != 0) { result = 1; goto done; }
    refresh_sensors(1); refresh_leds(); Fv_TerminalPump();
    ShowWindow(s_hwnd, SW_SHOW); SetFocus(s_hwnd); timer_init();
    DWORD last_readout = GetTickCount() - 250;
    int last_underrun = -1;
    int last_project = -1;
    while (s_running) {
        LARGE_INTEGER t0, t1; float dt; double ms; wchar_t status[512]; char title[512]; const char *name;
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
                MessageBoxW(s_hwnd, TR(L"适配器重载失败。请重新构建后启动。", L"Rebuild the adapter before restarting the preview."), TR(L"重载失败", L"Reload failed"), MB_OK | MB_ICONERROR);
                result = 1; break;
            }
            refresh_sensors(1); printf("[host] Adapter reloaded\n"); timer_init(); dt = 1.0f / s_cfg.fps;
        }
        if (s_paused && s_step_pending) {
            --s_step_pending;
            ad->Update(s_tx, s_ty, 1.0f / s_cfg.fps);
        } else if (!s_paused) {
            ad->Update(s_tx, s_ty, dt);
        }
        ad->Render();
        refresh_leds();
        if (s_debug) {
            char debug[256]; snprintf(debug, sizeof(debug), "%.0f fps %.1f ms tilt %.0f,%.0f%s", s_fps_now, s_last_ms, s_tx, s_ty, s_paused ? " PAUSED" : "");
            Fv_HostDrawText(debug, 10, s_surface.h - 32, 0xffe0);
        }
        /* Render still executes while paused, so its estimated writes still count. */
        compose(s_fps_now > 0 ? s_fps_now : s_cfg.fps);
        DWORD now = GetTickCount();
        int scanout_changed = last_underrun != s_stats.underrun;
        if (scanout_changed || last_project != s_current || now - last_readout >= 250) {
            last_readout = now;
            last_project = s_current;
            last_underrun = s_stats.underrun;
            swprintf(status, 512,
                TR(L"%dx%d RGB565 / %.0f Hz   读 %.2f / 写估 %.2f MB/s\r\n"
                   L"需求 %.2f / 实际 %.2f MB/s   缺失 %.1f%%（%lu 帧）",
                   L"%dx%d RGB565 / %.0f Hz   Read %.2f / Write est. %.2f MB/s\r\n"
                   L"Demand %.2f / Actual %.2f MB/s   Missing %.1f%% (%lu frames)"),
            s_surface.w, s_surface.h, s_cfg.memory.scan_hz, s_stats.read_mb_s, s_stats.write_mb_s, s_stats.demand_mb_s,
            s_stats.delivered_mb_s, 100.0 * s_stats.missing_pixels / ((double)s_surface.w * s_surface.h), s_underrun_frames);
            SetWindowTextW(s_status, status);
            name = ad->Title ? ad->Title() : NULL;
            snprintf(title, sizeof(title), "%s [%.0f fps]%s", name ? name : (s_cfg.title[0] ? s_cfg.title : "EmbeddedScreenPreview"), s_fps_now, s_paused ? " PAUSED" : "");
            SetWindowTextA(s_hwnd, title);
        }
        Fv_TerminalPump();
        if (s_want_shot) { s_want_shot = 0; take_screenshot(); }
        FvViewport v = preview_viewport();
        RECT frame = {v.x, scanout_changed ? 110 : v.y, v.x + v.w, v.y + v.h};
        if (scanout_changed) {
            RECT client;
            GetClientRect(s_hwnd, &client);
            frame.left = GUTTER;
            frame.right = client.right - PANEL - 2 * GUTTER;
        }
        InvalidateRect(s_hwnd, &frame, FALSE);
        UpdateWindow(s_hwnd);
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
    for (size_t i = 0; i < ACTION_COUNT; ++i) s_actions[i].window = NULL;
    DeleteObject(s_card_brush); DeleteObject(s_log_brush);
    DeleteObject(s_ui_font); DeleteObject(s_heading_font); DeleteObject(s_title_font); DeleteObject(s_log_font);
    destroy_surface(&s_surface); s_ad = NULL;
    return result;
}
