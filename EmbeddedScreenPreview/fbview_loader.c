/**
 ******************************************************************************
 * @file    fbview_loader.c
 * @brief   适配器加载：优先用现成 DLL，没有就现场用 gcc 编译
 *
 * 流程（Fv_LoadAdapter）：
 *   1. 读项目文件夹里的 preview.ini（可选）
 *      - 有 dll= 且文件存在    -> 直接 LoadLibrary
 *      - 有 sources= 且没 DLL  -> 调 gcc 编一个到 <folder>\_fbview\adapter.dll
 *      - 都没有                -> 在文件夹里搜 *_adapter.dll
 *   2. 解析 5 个 Pv_* 符号
 ******************************************************************************
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fbview_host.h"

/* ============================================================================
 * 小工具
 * ========================================================================== */

static void str_trim(char *s)
{
    int n, i = 0, j;

    if (s == NULL) { return; }
    n = (int)strlen(s);
    while (n > 0 && (s[n - 1] == '\r' || s[n - 1] == '\n' ||
                     s[n - 1] == ' '  || s[n - 1] == '\t'))
    {
        s[--n] = '\0';
    }
    while (s[i] == ' ' || s[i] == '\t') { i++; }
    if (i > 0)
    {
        for (j = 0; s[i + j] != '\0'; j++) { s[j] = s[i + j]; }
        s[j] = '\0';
    }
}

static int file_exists(const char *path)
{
    DWORD a = GetFileAttributesA(path);
    return (a != INVALID_FILE_ATTRIBUTES) && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static void dir_exists_or_create(const char *path)
{
    CreateDirectoryA(path, NULL);
}

/* ============================================================================
 * 读 preview.ini
 * ========================================================================== */

int Fv_ReadConfig(const char *folder, FvConfig *cfg)
{
    char path[MAX_PATH];
    FILE *fp;
    char line[1024];

    memset(cfg, 0, sizeof(*cfg));
    cfg->width  = 800;
    cfg->height = 480;
    cfg->fps    = 30;

    _snprintf(path, sizeof(path), "%s\\preview.ini", folder);
    path[sizeof(path) - 1] = '\0';

    fp = fopen(path, "rb");
    if (fp == NULL) { return 0; }        /* 没有 ini 很正常，用默认值 */

    while (fgets(line, (int)sizeof(line), fp) != NULL)
    {
        char *eq;

        str_trim(line);
        if (line[0] == '\0' || line[0] == '#' || line[0] == ';') { continue; }

        eq = strchr(line, '=');
        if (eq == NULL) { continue; }
        *eq = '\0';
        str_trim(line);
        str_trim(eq + 1);

        if      (strcmp(line, "width")  == 0) { cfg->width  = atoi(eq + 1); }
        else if (strcmp(line, "height") == 0) { cfg->height = atoi(eq + 1); }
        else if (strcmp(line, "fps")    == 0) { cfg->fps    = atoi(eq + 1); }
        else if (strcmp(line, "title")  == 0) { strncpy(cfg->title,  eq + 1, sizeof(cfg->title)  - 1); }
        else if (strcmp(line, "dll")    == 0) { strncpy(cfg->dll,    eq + 1, sizeof(cfg->dll)    - 1); }
        else if (strcmp(line, "sources")== 0) { strncpy(cfg->sources,eq + 1, sizeof(cfg->sources)- 1); }
        else if (strcmp(line, "includes")==0) { strncpy(cfg->includes,eq + 1,sizeof(cfg->includes)- 1); }
        else if (strcmp(line, "defines")== 0) { strncpy(cfg->defines, eq + 1, sizeof(cfg->defines)- 1); }
    }

    fclose(fp);

    if (cfg->width  <= 0) { cfg->width  = 800; }
    if (cfg->height <= 0) { cfg->height = 480; }
    if (cfg->fps    <= 0) { cfg->fps    = 30; }
    return 1;
}

/* ============================================================================
 * 运行外部命令（gcc），输出重定向到日志文件
 * ========================================================================== */

/* gcc 在含中文的路径下建临时文件会失败（"Cannot create temporary file"）。
 * 所以调 gcc 之前必须把 TMP/TEMP 指到一个【纯 ASCII 且可写】的目录。
 * 不依赖启动环境，逐个候选探测：真正写进去一个探针文件才算数。*/
static char s_tmp_dir[MAX_PATH] = { 0 };

static int dir_is_writable(const char *dir)
{
    char probe[MAX_PATH];
    HANDLE h;

    _snprintf(probe, sizeof(probe), "%s\\_fbview_probe.tmp", dir);
    probe[sizeof(probe) - 1] = '\0';

    h = CreateFileA(probe, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_TEMPORARY, NULL);
    if (h == INVALID_HANDLE_VALUE) { return 0; }

    CloseHandle(h);
    DeleteFileA(probe);
    return 1;
}

static void ensure_tmp_dir(void)
{
    char drive[8];
    DWORD len;
    int   i;

    if (s_tmp_dir[0] != '\0') { return; }

    len = GetEnvironmentVariableA("SystemDrive", drive, (DWORD)sizeof(drive));
    if (len == 0) { strcpy(drive, "C:"); }

    {
        char cand[4][MAX_PATH];

        _snprintf(cand[0], MAX_PATH, "%s\\pc_build", drive);
        _snprintf(cand[1], MAX_PATH, "%s\\pcbuild",  drive);
        _snprintf(cand[2], MAX_PATH, "%s\\Windows\\Temp", drive);

        len = GetEnvironmentVariableA("TEMP", cand[3], MAX_PATH);

        for (i = 0; i < 4; i++)
        {
            cand[i][MAX_PATH - 1] = '\0';
            if (cand[i][0] == '\0') { continue; }

            CreateDirectoryA(cand[i], NULL);      /* 已存在也无所谓 */

            /* 只接受纯 ASCII 且真能写的 */
            {
                int j, ascii_only = 1;
                for (j = 0; cand[i][j] != '\0'; j++)
                {
                    if ((unsigned char)cand[i][j] > 127) { ascii_only = 0; break; }
                }
                if (!ascii_only) { continue; }
            }

            if (dir_is_writable(cand[i]))
            {
                strncpy(s_tmp_dir, cand[i], sizeof(s_tmp_dir) - 1);
                s_tmp_dir[sizeof(s_tmp_dir) - 1] = '\0';
                break;
            }
        }
    }

    if (s_tmp_dir[0] != '\0')
    {
        SetEnvironmentVariableA("TMP",  s_tmp_dir);
        SetEnvironmentVariableA("TEMP", s_tmp_dir);
        printf("gcc temp dir: %s\n", s_tmp_dir);
    }
    else
    {
        printf("warning: no ASCII-writable temp dir found, gcc may fail\n");
    }
}
static int run_tool(const char *cmdline, const char *log_path, const char *work_dir)
{
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    DWORD  exit_code = (DWORD)-1;
    char  *full;
    int    ok;
    size_t need;

    /* 用 cmd.exe 做重定向，而不是给子进程塞句柄 ——
       句柄继承方式会让 gcc 的进程启动器报
       "Cannot create temporary file ... Permission denied"，很难查。*/
    need = strlen(cmdline) + strlen(log_path) + 64;
    full = (char *)malloc(need);
    if (full == NULL) { return -1; }

    _snprintf(full, need, "cmd.exe /c %s > \"%s\" 2>&1", cmdline, log_path);
    full[need - 1] = '\0';

    memset(&si, 0, sizeof(si));
    si.cb          = sizeof(si);
    si.dwFlags     = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    memset(&pi, 0, sizeof(pi));
    ok = CreateProcessA(NULL, full, NULL, NULL, FALSE, CREATE_NO_WINDOW,
                        NULL, work_dir, &si, &pi);
    free(full);

    if (ok)
    {
        WaitForSingleObject(pi.hProcess, INFINITE);
        GetExitCodeProcess(pi.hProcess, &exit_code);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    return (int)exit_code;
}
/* ============================================================================
 * 解析符号
 * ========================================================================== */

static int resolve_symbols(HMODULE dll, FvAdapter *out, char *err, int err_size)
{
    memset(out, 0, sizeof(*out));
    out->dll = dll;

    out->Attach = (int (*)(const FvHost *))GetProcAddress(dll, "Pv_Attach");
    out->Update = (void (*)(float, float, float))GetProcAddress(dll, "Pv_Update");
    out->Render = (void (*)(void))GetProcAddress(dll, "Pv_Render");
    out->Key    = (void (*)(int))GetProcAddress(dll, "Pv_Key");
    out->Title  = (const char *(*)(void))GetProcAddress(dll, "Pv_Title");

    if (!out->Attach || !out->Update || !out->Render)
    {
        if (err && err_size > 0)
        {
            _snprintf(err, (size_t)err_size,
                      "DLL 里缺少 Pv_Attach / Pv_Update / Pv_Render 导出\n"
                      "（编译时要在包含 fbview.h 之前定义 FBVIEW_BUILD_DLL）");
            err[err_size - 1] = '\0';
        }
        return -1;
    }
    return 0;
}

static HMODULE try_load(const char *path)
{
    HMODULE h;
    DWORD   e;

    SetLastError(0);
    h = LoadLibraryA(path);
    if (h == NULL)
    {
        e = GetLastError();
        printf("LoadLibrary(%s) failed, err=%lu\n", path, e);
    }
    return h;
}

/* ============================================================================
 * 在文件夹里搜索 *adapter*.dll
 * ========================================================================== */

static int find_dll_in_folder(const char *folder, char *out, int out_size)
{
    WIN32_FIND_DATAA fd;
    HANDLE h;
    char pattern[MAX_PATH];
    char best[MAX_PATH];
    int  found = 0;

    _snprintf(pattern, sizeof(pattern), "%s\\*.dll", folder);
    pattern[sizeof(pattern) - 1] = '\0';

    best[0] = '\0';
    h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) { return 0; }

    do
    {
        char lower[MAX_PATH];
        int i;

        strncpy(lower, fd.cFileName, sizeof(lower) - 1);
        lower[sizeof(lower) - 1] = '\0';
        for (i = 0; lower[i] != '\0'; i++)
        {
            if (lower[i] >= 'A' && lower[i] <= 'Z') { lower[i] = (char)(lower[i] + 32); }
        }

        /* 优先带 adapter 字样的 */
        if (strstr(lower, "adapter") != NULL)
        {
            _snprintf(best, sizeof(best), "%s\\%s", folder, fd.cFileName);
            best[sizeof(best) - 1] = '\0';
            found = 1;
            break;
        }
        if (!found)
        {
            _snprintf(best, sizeof(best), "%s\\%s", folder, fd.cFileName);
            best[sizeof(best) - 1] = '\0';
        }
    } while (FindNextFileA(h, &fd));

    FindClose(h);

    if (best[0] == '\0') { return 0; }
    {
        int n = (int)strlen(best);
        if (n > out_size - 1) { n = out_size - 1; }
        memcpy(out, best, (size_t)n);
        out[n] = '\0';
    }
    return 1;
}

/* ============================================================================
 * 主入口：加载适配器
 * ========================================================================== */

/* 记住上一次的路径和配置，供热键 R 重新加载用 */
static char     s_last_folder[MAX_PATH] = { 0 };
static FvConfig s_last_cfg;
static int      s_have_last = 0;

int Fv_ReloadAdapter(FvAdapter *out, char *err, int err_size)
{
    if (!s_have_last || out == NULL) { return -1; }

    /* 先卸掉旧 DLL，否则 LoadLibrary 会直接返回同一个句柄、代码还是旧的 */
    if (out->dll != NULL)
    {
        FreeLibrary((HMODULE)out->dll);
        out->dll = NULL;
    }
    memset(out, 0, sizeof(*out));

    return Fv_LoadAdapter(s_last_folder, &s_last_cfg, out, err, err_size);
}

int Fv_LoadAdapter(const char *folder, const FvConfig *cfg,
                   FvAdapter *out, char *err, int err_size)
{
    char path[MAX_PATH];
    char exe_dir[MAX_PATH];
    HMODULE dll = NULL;

    if (err && err_size > 0) { err[0] = '\0'; }
    if (folder == NULL || cfg == NULL || out == NULL) { return -1; }

    ensure_tmp_dir();

    /* 记住，供热键 R 用 */
    strncpy(s_last_folder, folder, sizeof(s_last_folder) - 1);
    s_last_folder[sizeof(s_last_folder) - 1] = '\0';
    s_last_cfg  = *cfg;
    s_have_last = 1;

    /* ---------- ① ini 里指定了 dll ---------- */
    if (cfg->dll[0] != '\0')
    {
        _snprintf(path, sizeof(path), "%s\\%s", folder, cfg->dll);
        path[sizeof(path) - 1] = '\0';
        if (file_exists(path)) { dll = try_load(path); }
    }

    /* ---------- ② 文件夹里找 *_adapter.dll ---------- */
    if (dll == NULL)
    {
        char found[MAX_PATH];
        if (find_dll_in_folder(folder, found, (int)sizeof(found)))
        {
            dll = try_load(found);
        }
    }

    /* ---------- ③ 没有 DLL，看 ini 里有没有 sources，现场编 ---------- */
    if (dll == NULL && cfg->sources[0] != '\0')
    {
        char cmd[4096];
        char dll_path[MAX_PATH];
        char log_path[MAX_PATH];
        char build_dir[MAX_PATH];
        char inc_flags[1024];
        char def_flags[512];
        char *tok;
        int   rc;

        GetModuleFileNameA(NULL, exe_dir, (DWORD)sizeof(exe_dir));
        {
            char *p = strrchr(exe_dir, '\\');
            if (p != NULL) { *p = '\0'; }
        }

        _snprintf(build_dir, sizeof(build_dir), "%s\\_fbview", folder);
        build_dir[sizeof(build_dir) - 1] = '\0';
        dir_exists_or_create(build_dir);

        _snprintf(dll_path, sizeof(dll_path), "%s\\adapter.dll", build_dir);
        dll_path[sizeof(dll_path) - 1] = '\0';
        _snprintf(log_path, sizeof(log_path), "%s\\build.log", build_dir);
        log_path[sizeof(log_path) - 1] = '\0';

        /* includes: 分号或逗号分隔，相对 folder */
        inc_flags[0] = '\0';
        strncat(inc_flags, "-I\"", sizeof(inc_flags) - strlen(inc_flags) - 1);
        strncat(inc_flags, exe_dir, sizeof(inc_flags) - strlen(inc_flags) - 1);
        strncat(inc_flags, "\" ", sizeof(inc_flags) - strlen(inc_flags) - 1);
        {
            char tmp[512];
            strncpy(tmp, cfg->includes, sizeof(tmp) - 1);
            tmp[sizeof(tmp) - 1] = '\0';
            tok = strtok(tmp, ";,");
            while (tok != NULL)
            {
                str_trim(tok);
                if (tok[0] != '\0')
                {
                    strncat(inc_flags, "-I\"", sizeof(inc_flags) - strlen(inc_flags) - 1);
                    if (tok[0] != '\\' && (tok[1] != ':' && tok[0] != '/'))
                    {
                        strncat(inc_flags, folder, sizeof(inc_flags) - strlen(inc_flags) - 1);
                        strncat(inc_flags, "\\", sizeof(inc_flags) - strlen(inc_flags) - 1);
                    }
                    strncat(inc_flags, tok, sizeof(inc_flags) - strlen(inc_flags) - 1);
                    strncat(inc_flags, "\" ", sizeof(inc_flags) - strlen(inc_flags) - 1);
                }
                tok = strtok(NULL, ";,");
            }
        }

        /* defines: 空格或逗号分隔 */
        def_flags[0] = '\0';
        {
            char tmp[512];
            strncpy(tmp, cfg->defines, sizeof(tmp) - 1);
            tmp[sizeof(tmp) - 1] = '\0';
            tok = strtok(tmp, " ;,");
            while (tok != NULL)
            {
                strncat(def_flags, "-D", sizeof(def_flags) - strlen(def_flags) - 1);
                strncat(def_flags, tok, sizeof(def_flags) - strlen(def_flags) - 1);
                strncat(def_flags, " ", sizeof(def_flags) - strlen(def_flags) - 1);
                tok = strtok(NULL, " ;,");
            }
        }

        DeleteFileA(dll_path);

        /* 拼 gcc 命令行：源文件相对 folder */
        _snprintf(cmd, sizeof(cmd),
                  "gcc -std=c11 -O2 -shared -DFBVIEW_BUILD_DLL %s %s "
                  "-o \"%s\" ",
                  inc_flags, def_flags, dll_path);
        cmd[sizeof(cmd) - 1] = '\0';

        {
            char tmp[2048];
            strncpy(tmp, cfg->sources, sizeof(tmp) - 1);
            tmp[sizeof(tmp) - 1] = '\0';
            tok = strtok(tmp, ";,");
            while (tok != NULL)
            {
                str_trim(tok);
                if (tok[0] != '\0')
                {
                    strncat(cmd, "\"", sizeof(cmd) - strlen(cmd) - 1);
                    if (tok[0] != '\\' && (tok[1] != ':' && tok[0] != '/'))
                    {
                        strncat(cmd, folder, sizeof(cmd) - strlen(cmd) - 1);
                        strncat(cmd, "\\", sizeof(cmd) - strlen(cmd) - 1);
                    }
                    strncat(cmd, tok, sizeof(cmd) - strlen(cmd) - 1);
                    strncat(cmd, "\" ", sizeof(cmd) - strlen(cmd) - 1);
                }
                tok = strtok(NULL, ";,");
            }
        }

        printf("no adapter dll found, compiling...\n");
        printf("  %s\n", cmd);
        fflush(stdout);

        rc = run_tool(cmd, log_path, NULL);
        printf("gcc exit = %d  (log: %s)\n", rc, log_path);
        fflush(stdout);

        if (rc != 0)
        {
            if (err && err_size > 0)
            {
                _snprintf(err, (size_t)err_size,
                          "gcc 编译失败（exit=%d），详见日志：\n%s", rc, log_path);
                err[err_size - 1] = '\0';
            }
            return -1;
        }

        dll = try_load(dll_path);
        if (dll == NULL)
        {
            if (err && err_size > 0)
            {
                _snprintf(err, (size_t)err_size, "编译成功但 LoadLibrary 失败：\n%s", dll_path);
                err[err_size - 1] = '\0';
            }
            return -1;
        }
    }

    if (dll == NULL)
    {
        if (err && err_size > 0)
        {
            _snprintf(err, (size_t)err_size,
                      "这个文件夹里没有可用的适配器。\n\n"
                      "需要以下之一：\n"
                      "  - 一个导出 Pv_* 的 DLL（名字里带 adapter 更好认）\n"
                      "  - 一个 preview.ini，里面用 sources= 列出要编译的源文件");
            err[err_size - 1] = '\0';
        }
        return -1;
    }

    return resolve_symbols(dll, out, err, err_size);
}
