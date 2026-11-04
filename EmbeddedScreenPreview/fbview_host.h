/**
 * @file    fbview_host.h
 * @brief   宿主内部接口（不对外，适配器不需要这个文件）
 */
#ifndef __FBVIEW_HOST_H
#define __FBVIEW_HOST_H

#include "fbview.h"

/* 从 DLL 里解析出来的 5 个函数指针 */
typedef struct
{
    int         (*Attach)(const FvHost *host);
    void        (*Update)(float tx, float ty, float dt);
    void        (*Render)(void);
    void        (*Key)(int key);
    const char *(*Title)(void);

    void *dll;              /* HMODULE，仅内部用 */
} FvAdapter;

/* preview.ini 里的配置 */
typedef struct
{
    int  width;
    int  height;
    int  fps;
    char title[64];
    char dll[260];           /* 指定 DLL 文件名（相对 folder）*/
    char sources[1024];      /* 没 DLL 时用它编译，逗号分隔 */
    char includes[512];      /* 额外的 -I，逗号分隔（相对 folder）*/
    char defines[512];       /* 额外的 -D */
} FvConfig;

/* ---------------- fbview_loader.c ---------------- */

/** 读 <folder>\preview.ini。没有 ini 时用默认值，返回 0 */
int  Fv_ReadConfig(const char *folder, FvConfig *cfg);

/** 在 folder 里找适配器：有 DLL 就直接用，没有就现场用 gcc 编译。
 *  @return 0 = 成功 */
int  Fv_LoadAdapter(const char *folder, const FvConfig *cfg,
                    FvAdapter *out, char *err, int err_size);

/** 按上次的 folder/config 重新加载适配器（热键 R）。
 *  先把旧 DLL 卸掉，再走一遍 Fv_LoadAdapter。
 *  @return 0 = 成功（成功后调用方要重新 Attach）*/
int  Fv_ReloadAdapter(FvAdapter *out, char *err, int err_size);

/* ---------------- fbview.c ---------------- */

/** 开窗口跑起来。返回 0 = 正常退出 */
int  Fv_RunHost(int w, int h, int fps, FvAdapter *ad, const char *title);

/** 不开窗口，渲染一帧并导出 BMP。返回 0 = 成功 */
int  Fv_RunHeadless(int w, int h, FvAdapter *ad, const char *bmp_path);

/** 画一行文字（宿主内部用，也通过 FvHost.draw_text 给适配器）*/
void Fv_HostDrawText(const char *s, int x, int y, uint16_t c565);

#endif /* __FBVIEW_HOST_H */
