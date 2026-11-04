/**
 ******************************************************************************
 * @file    demo_adapter.c
 * @brief   EmbeddedScreenPreview 的最小适配器示例 —— 证明模拟器不依赖任何具体项目
 *
 * 棋盘格 + 一个被键盘倾斜推着走的小方块，和 STM32 游戏毫无关系。
 *
 * 编译成 DLL：
 *   gcc -std=c11 -O2 -shared -DFBVIEW_BUILD_DLL -I. -o demo_adapter.dll demo_adapter.c
 ******************************************************************************
 */
#ifndef FBVIEW_BUILD_DLL
#define FBVIEW_BUILD_DLL
#endif
#include "fbview.h"

#include <stdio.h>
#include <string.h>

static const FvHost *s_host = NULL;
static float s_x = 400.0f, s_y = 240.0f;
static float s_vx = 0.0f, s_vy = 0.0f;
static int   s_count = 0;
static char  s_title[160];
static float s_temperature = 25, s_humidity = 50, s_light = 500;

void Pv_FillRect(uint16_t *fb, int fb_w, int fb_h,
                 int x, int y, int w, int h, uint16_t c565)
{
    int i, j;
    for (j = y; j < y + h; j++) {
        if (j < 0 || j >= fb_h) { continue; }
        for (i = x; i < x + w; i++) {
            if (i < 0 || i >= fb_w) { continue; }
            fb[j * fb_w + i] = c565;
        }
    }
}

int Pv_Attach(const FvHost *host)
{
    s_host = host;
    if (host == NULL || host->fb == NULL) { return -1; }
    s_x = host->w / 2.0f;
    s_y = host->h / 2.0f;
    s_vx = s_vy = 0; s_count = 0;
    printf("demo adapter attached: %dx%d\n", host->w, host->h);
    return 0;
}

void Pv_Update(float tilt_x, float tilt_y, float dt)
{
    s_vx += (tilt_x / 6300.0f) * 900.0f * dt;
    s_vy += (tilt_y / 6300.0f) * 900.0f * dt;
    s_vx *= 0.96f;
    s_vy *= 0.96f;

    s_x += s_vx * dt;
    s_y += s_vy * dt;

    if (s_x < 30.0f)                    { s_x = 30.0f;                    s_vx = -s_vx * 0.8f; }
    if (s_x > (float)s_host->w - 30.0f) { s_x = (float)s_host->w - 30.0f; s_vx = -s_vx * 0.8f; }
    if (s_y < 30.0f)                    { s_y = 30.0f;                    s_vy = -s_vy * 0.8f; }
    if (s_y > (float)s_host->h - 30.0f) { s_y = (float)s_host->h - 30.0f; s_vy = -s_vy * 0.8f; }
}

void Pv_Render(void)
{
    uint16_t *fb = s_host->fb;
    int w = s_host->w, h = s_host->h;
    int x, y;

    for (y = 0; y < h; y += 32) {
        for (x = 0; x < w; x += 32) {
            uint16_t c = (((x / 32) + (y / 32)) & 1) ? 0x18A0 : 0x1060;
            Pv_FillRect(fb, w, h, x, y, 32, 32, c);
        }
    }

    Pv_FillRect(fb, w, h, w / 2 - 1, 0, 2, h, 0x2104);
    Pv_FillRect(fb, w, h, 0, h / 2 - 1, w, 2, 0x2104);
    Pv_FillRect(fb, w, h, (int)s_x - 20, (int)s_y - 20, 40, 40, 0xFFE0);
    Pv_FillRect(fb, w, h, (int)s_x - 16, (int)s_y - 16, 32, 32, 0x001F);

    if (s_host->draw_text) {
        char buf[96];
        sprintf(buf, "EmbeddedScreenPreview demo   %d keys   tilt %.0f,%.0f",
                s_count, s_host->tilt_x, s_host->tilt_y);
        s_host->draw_text(buf, 10, 8, 0xFFFF);
        snprintf(buf, sizeof(buf), "Temp %.0f C   Humidity %.0f %%   Light %.0f lux", s_temperature, s_humidity, s_light);
        s_host->draw_text(buf, 10, 36, s_temperature >= 60 ? 0xf800 : 0xffff);
    }
}

void Pv_Key(int key)
{
    s_count++;
    printf("KEY%d (total %d)\n", key, s_count);
    if (key == 2) {
        s_x = s_host->w / 2.0f;
        s_y = s_host->h / 2.0f;
        s_vx = s_vy = 0.0f;
    }
}

const char *Pv_Title(void)
{
    sprintf(s_title, "EmbeddedScreenPreview demo   pos=(%.0f,%.0f)   keys=%d", s_x, s_y, s_count);
    return s_title;
}
void Pv_SetSensor(int sensor, float value)
{
    float *target = sensor == FV_SENSOR_TEMPERATURE ? &s_temperature :
                    sensor == FV_SENSOR_HUMIDITY ? &s_humidity :
                    sensor == FV_SENSOR_LIGHT ? &s_light : NULL;
    if (!target || *target == value) return;
    *target = value;
    printf("[sensor] id=%d value=%.1f\n", sensor, value);
}
void Pv_Touch(int x, int y, int pressed)
{
    if (pressed < 0) return;
    s_x = (float)x; s_y = (float)y; s_vx = s_vy = 0;
    if (!pressed) printf("[touch] x=%d y=%d\n", x, y);
}
