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
static unsigned s_extra_present;
static float s_extra_values[4];
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
    s_vx = s_vy = 0; s_count = 0; s_extra_present = 0;
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
        static const char *formats[] = {"Pressure %.0f hPa", "Distance %.0f cm", "Voltage %.0f mV", "Potentiometer %.0f ADC"};
        int line = 64;
        for (int i = 0; i < 4; ++i) if (s_extra_present & (1u << i)) {
            snprintf(buf, sizeof(buf), formats[i], s_extra_values[i]);
            s_host->draw_text(buf, 10, line, 0xffff);
            line += 27;
        }
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
    if (sensor >= FV_SENSOR_PRESSURE && sensor <= FV_SENSOR_POTENTIOMETER) {
        unsigned i = (unsigned)(sensor - FV_SENSOR_PRESSURE);
        unsigned bit = 1u << i;
        if ((s_extra_present & bit) && s_extra_values[i] == value) return;
        s_extra_present |= bit;
        target = &s_extra_values[i];
    }
    if (!target) return;
    if (sensor < FV_SENSOR_PRESSURE && *target == value) return;
    *target = value;
    printf("[sensor] id=%d value=%.1f\n", sensor, value);
}
void Pv_Touch(int x, int y, int pressed)
{
    if (pressed < 0) return;
    s_x = (float)x; s_y = (float)y; s_vx = s_vy = 0;
    if (!pressed) printf("[touch] x=%d y=%d\n", x, y);
}

uint32_t Pv_LedColor(unsigned index)
{
    switch (index) {
    case 0: return s_temperature >= 60 ? 0xf64b37 : 0x28ce91;
    case 1: return s_humidity >= 80 ? 0xba76ee : 0x4297fa;
    case 2: {
        unsigned pot = FV_SENSOR_POTENTIOMETER - FV_SENSOR_PRESSURE;
        float level = (s_extra_present & (1u << pot)) ? s_extra_values[pot] / 4095.0f : s_light / 500.0f;
        if (level <= 0) return 0;
        if (level > 1) level = 1;
        return ((uint32_t)(255 * level) << 16) | ((uint32_t)(180 * level) << 8) | (uint32_t)(46 * level);
    }
    default: return 0;
    }
}

void Pv_SerialRx(const char *bytes, size_t length)
{
    while (length && (bytes[length - 1] == '\r' || bytes[length - 1] == '\n')) --length;
    if (length == 4 && memcmp(bytes, "help", 4) == 0) {
        puts("[demo] commands: help, status, echo <text>");
    } else if (length == 6 && memcmp(bytes, "status", 6) == 0) {
        printf("[demo] Temperature %.0f C, humidity %.0f %%, light %.0f lux\n",
               s_temperature, s_humidity, s_light);
        printf("[demo] LED 1 #%06lX, LED 2 #%06lX, LED 3 #%06lX\n",
               (unsigned long)Pv_LedColor(0), (unsigned long)Pv_LedColor(1), (unsigned long)Pv_LedColor(2));
    } else if (length >= 5 && memcmp(bytes, "echo ", 5) == 0) {
        fputs("[demo] ", stdout);
        fwrite(bytes + 5, 1, length - 5, stdout);
        putchar('\n');
    } else {
        puts("[demo] Unknown command. Send help for the list.");
    }
}
