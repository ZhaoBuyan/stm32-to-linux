#include "fv_sim.h"
#include <math.h>
FvViewport Fv_FitViewport(int cw, int ch, int top, int bottom, int w, int h)
{
    FvViewport r = {0, top, 0, 0};
    int ah = ch - top - bottom;
    if (cw <= 0 || ah <= 0 || w <= 0 || h <= 0) return r;
    if ((double)cw * h <= (double)ah * w) {
        r.w = cw; r.h = (int)((double)cw * h / w);
    } else {
        r.h = ah; r.w = (int)((double)ah * w / h);
    }
    r.x = (cw - r.w) / 2; r.y = top + (ah - r.h) / 2;
    return r;
}
int Fv_MapTouch(FvViewport v, int w, int h, int px, int py, int *x, int *y)
{
    if (!x || !y || w <= 0 || h <= 0 || v.w <= 0 || v.h <= 0 ||
        px < v.x || py < v.y || px >= v.x + v.w || py >= v.y + v.h) return 0;
    *x = (int)((long long)(px - v.x) * w / v.w);
    *y = (int)((long long)(py - v.y) * h / v.h);
    return 1;
}
FvMemoryStats Fv_ApplyMemory(uint32_t *pixels, size_t n, double hz,
                           const FvMemoryConfig *cfg)
{
    FvMemoryStats s = {0};
    double fraction = 1.0, credit = 0.0;
    uint32_t held = 0;
    size_t i;
    if (!cfg || !pixels || !n || !isfinite(hz) || hz < 0 ||
        !isfinite(cfg->scan_hz) || cfg->scan_hz <= 0) return s;
    s.read_mb_s = (double)n * 2 * cfg->scan_hz / 1000000.0;
    s.write_mb_s = (double)n * 2 * hz * cfg->write_passes / 1000000.0;
    s.demand_mb_s = s.read_mb_s + s.write_mb_s;
    s.delivered_mb_s = s.demand_mb_s;
    if (cfg->limit_mb_s > 0 && s.demand_mb_s > cfg->limit_mb_s) {
        fraction = (cfg->limit_mb_s - s.write_mb_s) / s.read_mb_s;
        if (fraction < 0) fraction = 0;
        s.delivered_mb_s = cfg->limit_mb_s;
    }
    /* RGB565 read bursts of 32 pixels (64 bytes). Accumulate grants so a
       fractional budget produces deterministic horizontal streaks. */
    for (i = 0; i < n; i += 32) {
        size_t j, end = n - i < 32 ? n : i + 32;
        credit += fraction;
        if (credit >= 1.0 - 1e-12) {
            credit -= 1.0;
            held = pixels[end - 1];
        } else {
            for (j = i; j < end; ++j) pixels[j] = held;
            s.missing_pixels += end - i;
        }
    }
    s.underrun = s.missing_pixels != 0;
    return s;
}
