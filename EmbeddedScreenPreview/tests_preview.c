/* PC adapter regressions: calibrated directions, touch, persistent LCD text. */
#include "fbview.c"
#include "../STM32F429/Game_LCD/User/game/preview_adapter.c"
#include <assert.h>
static void start_game(void)
{
    int x, y;
    s_text_count = 0;
    assert(Pv_Attach(&s_host) == 0);
    x = s_app.buttons[0].rect.x + s_app.buttons[0].rect.w/2;
    y = s_app.buttons[0].rect.y + s_app.buttons[0].rect.h/2;
    Pv_Touch(x, y, 1); assert(s_app.game.state == GAME_READY);
    Pv_Touch(x, y, 0); assert(s_app.game.state == GAME_RUNNING);
}
int main(void)
{
    uint16_t header[800*50];
    s_cfg.width = 800; s_cfg.height = 480; s_cfg.fps = 30;
    s_cfg.memory.scan_hz = 60; s_cfg.memory.write_passes = 1;
    assert(create_surface(&s_surface, 800, 480) == 0); bind_surface();
    assert(Pv_Attach(&s_host) == 0); Pv_Render();
    memcpy(header, s_surface.fb, sizeof(header));
    compose(30);
    assert(memcmp(header, s_surface.fb, sizeof(header)) != 0);
    memcpy(header, s_surface.fb, sizeof(header));
    Pv_Render(); compose(30);
    assert(memcmp(header, s_surface.fb, sizeof(header)) == 0);
    start_game(); Pv_Update(-6300, 0, 1.0f/30); assert(s_app.game.ball.vx < 0); /* A */
    start_game(); Pv_Update(6300, 0, 1.0f/30); assert(s_app.game.ball.vx > 0); /* D */
    start_game(); Pv_Update(0, -6300, 1.0f/30); assert(s_app.game.ball.vy < 0); /* W */
    start_game(); Pv_Update(0, 6300, 1.0f/30); assert(s_app.game.ball.vy > 0); /* S */
    int x = s_app.buttons[0].rect.x + s_app.buttons[0].rect.w/2;
    int y = s_app.buttons[0].rect.y + s_app.buttons[0].rect.h/2;
    Pv_Touch(x, y, -1); assert(s_app.game.state == GAME_RUNNING);
    Pv_Touch(x, y, 0); assert(s_app.game.state == GAME_PAUSED);
    destroy_surface(&s_surface);
    puts("PASS: A/D and W/S directions, touch start/pause/cancel, persistent letters");
    return 0;
}
