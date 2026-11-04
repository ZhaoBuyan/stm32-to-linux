/**
 ******************************************************************************
 * @file    fbview_main.c
 * @brief   PCFbView 入口：选项目文件夹 -> 加载(或现场编译)适配器 -> 跑
 *
 * 用法：
 *   fbview.exe                            弹出文件夹选择框
 *   fbview.exe -dir <项目文件夹>           直接打开
 *   fbview.exe -dir <项目文件夹> -export a.bmp   不开窗口，导出一帧
 *   fbview.exe -list                      只检查能不能加载，不运行
 ******************************************************************************
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fbview_host.h"

/* ============================================================================
 * 文件夹选择框
 * ========================================================================== */
static int pick_folder(char *out, int out_size)
{
    BROWSEINFOA bi;
    LPITEMIDLIST pidl;
    int ok = 0;

    memset(&bi, 0, sizeof(bi));
    bi.hwndOwner = GetConsoleWindow();
    bi.lpszTitle = "选择要预览的项目文件夹（里面要有 adapter DLL 或 preview.ini）";
    bi.ulFlags   = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    pidl = SHBrowseForFolderA(&bi);
    if (pidl != NULL)
    {
        if (SHGetPathFromIDListA(pidl, out))
        {
            ok = 1;
        }
        CoTaskMemFree(pidl);
    }

    if (!ok && out_size > 0) { out[0] = '\0'; }
    return ok;
}

/* ============================================================================
 * 错误提示
 * ========================================================================== */
static void show_error(const char *msg)
{
    /* msg 是 UTF-8，MessageBoxW 要 UTF-16，必须转换，否则中文是乱码 */
    wchar_t wbuf[2048];
    int n;

    printf("%s\n", msg);
    fflush(stdout);

    n = MultiByteToWideChar(CP_UTF8, 0, msg, -1, wbuf,
                            (int)(sizeof(wbuf) / sizeof(wbuf[0])));
    if (n <= 0)
    {
        MessageBoxA(NULL, msg, "PCFbView", MB_OK | MB_ICONERROR);
        return;
    }
    MessageBoxW(NULL, wbuf, L"PCFbView", MB_OK | MB_ICONERROR);
}

/* ============================================================================
 * main
 * ========================================================================== */

int main(int argc, char **argv)
{
    char       folder[MAX_PATH];
    char       export_path[MAX_PATH];
    char       err[1024];
    FvConfig   cfg;
    FvAdapter  adapter;
    const char *title;
    int        i;
    int        list_only = 0;
    int        have_export = 0;

    folder[0] = '\0';
    export_path[0] = '\0';

    for (i = 1; i < argc; i++)
    {
        if      (strcmp(argv[i], "-dir") == 0 && i + 1 < argc)
        {
            strncpy(folder, argv[++i], sizeof(folder) - 1);
            folder[sizeof(folder) - 1] = '\0';
        }
        else if (strcmp(argv[i], "-export") == 0 && i + 1 < argc)
        {
            strncpy(export_path, argv[++i], sizeof(export_path) - 1);
            export_path[sizeof(export_path) - 1] = '\0';
            have_export = 1;
        }
        else if (strcmp(argv[i], "-list") == 0) { list_only = 1; }
        else if (strcmp(argv[i], "-?") == 0 || strcmp(argv[i], "--help") == 0)
        {
            printf("usage: %s [-dir <folder>] [-export <bmp>] [-list]\n", argv[0]);
            return 0;
        }
    }

    /* ---------- ① 选文件夹 ---------- */
    if (folder[0] == '\0')
    {
        if (!pick_folder(folder, (int)sizeof(folder)))
        {
            printf("没有选择文件夹，退出。\n");
            return 0;
        }
    }

    {
        DWORD a = GetFileAttributesA(folder);
        if (a == INVALID_FILE_ATTRIBUTES || !(a & FILE_ATTRIBUTE_DIRECTORY))
        {
            show_error("这个路径不是有效的文件夹。");
            return 1;
        }
    }

    printf("project folder: %s\n", folder);

    /* ---------- ② 读配置 ---------- */
    Fv_ReadConfig(folder, &cfg);
    printf("config: %dx%d @%dfps", cfg.width, cfg.height, cfg.fps);
    if (cfg.title[0] != '\0') { printf("  title=%s", cfg.title); }
    printf("\n");

    /* ---------- ③ 加载适配器 ---------- */
    if (Fv_LoadAdapter(folder, &cfg, &adapter, err, (int)sizeof(err)) != 0)
    {
        show_error(err[0] != '\0' ? err : "加载适配器失败。");
        return 1;
    }

    printf("adapter loaded OK.\n");

    if (list_only)
    {
        printf("(-list) 只检查，不运行。\n");
        return 0;
    }

    /* ---------- ④ 跑 ---------- */
    title = (cfg.title[0] != '\0') ? cfg.title : "PCFbView";

    if (have_export)
    {
        return Fv_RunHeadless(cfg.width, cfg.height, &adapter, export_path);
    }

    return Fv_RunHost(cfg.width, cfg.height, cfg.fps, &adapter, title);
}
