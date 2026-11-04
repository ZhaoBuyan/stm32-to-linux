#ifndef FV_SIM_H
#define FV_SIM_H
#include <stddef.h>
#include <stdint.h>
typedef struct {
    double limit_mb_s;       /* decimal MB/s; 0 means unlimited */
    double scan_hz;          /* panel refresh, independent of host FPS */
    double write_passes;     /* estimated full framebuffer writes per render */
} FvMemoryConfig;
typedef struct {
    double read_mb_s, write_mb_s, demand_mb_s, delivered_mb_s;
    size_t missing_pixels;
    int underrun;
} FvMemoryStats;
typedef struct { int x, y, w, h; } FvViewport;
FvViewport Fv_FitViewport(int client_w, int client_h, int top, int bottom, int w, int h);
int Fv_MapTouch(FvViewport v, int w, int h, int px, int py, int *x, int *y);
/* Frame-level contention model: writes have priority; missing 64-byte reads hold
 * the last delivered pixel. This is a teaching model, not cycle-accurate FMC. */
FvMemoryStats Fv_ApplyMemory(uint32_t *pixels, size_t count, double render_hz,
                           const FvMemoryConfig *cfg);
#endif
