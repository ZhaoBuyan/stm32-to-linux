/* Real DLL integration, including different framebuffer dimensions. */
#include "fbview.c"
#include <assert.h>
int main(int argc, char **argv)
{
    FvAdapter ad = {0}; char err[1024]; int small, bad; uint16_t *old_fb;
    assert(argc == 4);
    Fv_ReadConfig(argv[1], &s_cfg);
    assert(Fv_LoadAdapter(argv[1], &s_cfg, &ad, err, sizeof(err)) == 0);
    s_ad = &ad; s_current = Fv_AddProject(argv[1]);
    strcpy(s_folder, s_folders[s_current]);
    assert(create_surface(&s_surface, s_cfg.width, s_cfg.height) == 0);
    bind_surface(); assert(ad.Attach(&s_host) == 0);
    assert(ad.SetSensor && ad.LedColor);
    refresh_sensors(1); refresh_leds();
    uint32_t normal[FV_LED_COUNT];
    memcpy(normal, s_led_colors, sizeof(normal));
    ad.SetSensor(FV_SENSOR_TEMPERATURE, 75);
    ad.SetSensor(FV_SENSOR_HUMIDITY, 90);
    ad.SetSensor(FV_SENSOR_POTENTIOMETER, 0);
    refresh_leds();
    assert(s_led_colors[0] != normal[0] && s_led_colors[1] != normal[1]);
    assert(s_led_colors[2] == 0);
    ad.SetSensor(FV_SENSOR_POTENTIOMETER, 4095);
    refresh_leds();
    assert(s_led_colors[2] == 0xffb42e);
    assert(ad.LedColor(FV_LED_COUNT) == 0);
    timer_init();
    small = Fv_AddProject(argv[2]); assert(small >= 0);
    assert(Fv_AddProject(argv[2]) == small); /* canonical deduplication */
    assert(switch_project(small) == 0);
    assert(s_surface.w == 320 && s_surface.h == 240 && s_host.fps == 15);
    assert(s_cfg.memory.limit_mb_s == 12 && s_cfg.memory.scan_hz == 50);
    ad.Update(0, 0, 1.0f/15); ad.Render(); compose(15);
    assert(!s_stats.underrun);
    old_fb = s_host.fb;
    bad = Fv_AddProject(argv[3]);
    assert(switch_project(bad) != 0 && s_host.fb == old_fb && s_current == small);
    ad.Render(); compose(15); /* the previous adapter remains callable */
    assert(switch_project(0) == 0 && s_surface.w == 800 && s_surface.h == 480);
    ad.Render(); compose(30);
    destroy_surface(&s_surface); FreeLibrary((HMODULE)ad.dll);
    puts("PASS: project switching, resolution/FPS/config change, rollback, recovery");
    return 0;
}
