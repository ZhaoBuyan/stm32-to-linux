#include "fv_sim.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
int main(void)
{
    size_t n = 800 * 480;
    uint32_t *p = malloc(n * sizeof(*p));
    FvMemoryConfig cfg = {0, 60, 1}; FvMemoryStats s;
    FvViewport v = Fv_FitViewport(1000, 1000, 40, 48, 800, 480);
    int x, y;
    assert(Fv_MapTouch(v, 800, 480, 500, 496, &x, &y) && x == 400 && y == 240);
    assert(!Fv_MapTouch(v, 800, 480, 500, 195, &x, &y));
    assert(!Fv_MapTouch(v, 800, 480, 1000, 496, &x, &y));
    assert(p && v.w == 1000 && v.h == 600 && v.x == 0 && v.y == 196);
    v = Fv_FitViewport(1600, 568, 40, 48, 800, 480);
    assert(v.w == 800 && v.h == 480 && v.x == 400 && v.y == 40);
    v = Fv_FitViewport(0, 0, 40, 48, 800, 480); assert(!v.w && !v.h);
    for (size_t i=0; i<n; ++i) p[i] = (uint32_t)i + 1;
    s = Fv_ApplyMemory(p, n, 30, &cfg);
    assert(!s.underrun && fabs(s.read_mb_s - 46.08) < 1e-6);
    assert(fabs(s.write_mb_s - 23.04) < 1e-6);
    assert(fabs(s.demand_mb_s - 69.12) < 1e-6);
    for (size_t i=0; i<n; ++i) assert(p[i] == i + 1);
    cfg.limit_mb_s = 69.12;
    s = Fv_ApplyMemory(p, n, 30, &cfg); assert(!s.underrun);
    cfg.limit_mb_s = 46.08;
    s = Fv_ApplyMemory(p, n, 30, &cfg);
    assert(s.underrun && s.missing_pixels == n/2 && p[0] == 0);
    assert(s.delivered_mb_s <= cfg.limit_mb_s);
    cfg.limit_mb_s = 20;
    s = Fv_ApplyMemory(p, n, 30, &cfg); assert(s.missing_pixels == n);
    cfg.limit_mb_s = 80;
    for (size_t i=0; i<n; ++i) p[i] = 0xffffff;
    s = Fv_ApplyMemory(p, n, 30, &cfg); assert(!s.underrun && p[0] == 0xffffff);
    cfg.scan_hz = 120;
    s = Fv_ApplyMemory(p, n, 30, &cfg); assert(s.underrun);
    cfg.scan_hz = 60; cfg.write_passes = 0;
    s = Fv_ApplyMemory(p, n, 30, &cfg); assert(!s.underrun && s.write_mb_s == 0);
    cfg.limit_mb_s = 0;
    s = Fv_ApplyMemory(p, n, 240, &cfg); assert(!s.underrun);
    free(p); puts("PASS: viewport, bandwidth thresholds, starvation, recovery, scan refresh");
    return 0;
}
