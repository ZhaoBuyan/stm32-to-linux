#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <io.h>
#include <wchar.h>
#include <string.h>
#include "fv_terminal.h"
static FILE *s_writer, *s_reader;
static int s_stdout = -1, s_stderr = -1;
static HWND s_edit;
static wchar_t s_text[65536];
static size_t s_length;
static unsigned char s_tail[4];
static size_t s_tail_size;
int Fv_TerminalStart(HWND edit)
{
    wchar_t path[MAX_PATH], *slash;
    if (!GetModuleFileNameW(NULL, path, MAX_PATH)) return -1;
    slash = wcsrchr(path, L'\\'); if (!slash) return -1;
    *slash = 0;
    if (wcslen(path) + 50 >= MAX_PATH) return -1;
    wcscat(path, L"\\_fbview"); CreateDirectoryW(path, NULL);
    swprintf(path + wcslen(path), 50, L"\\session-%lu.log", GetCurrentProcessId());
    s_writer = _wfopen(path, L"wb");
    if (!s_writer) return -1;
    s_reader = _wfopen(path, L"rb");
    if (!s_reader) { fclose(s_writer); s_writer = NULL; return -1; }
    fflush(stdout); fflush(stderr);
    s_stdout = _dup(_fileno(stdout)); s_stderr = _dup(_fileno(stderr));
    if (s_stdout < 0 || s_stderr < 0) { Fv_TerminalStop(); return -1; }
    if (_dup2(_fileno(s_writer), _fileno(stdout)) != 0 ||
        _dup2(_fileno(s_writer), _fileno(stderr)) != 0) { Fv_TerminalStop(); return -1; }
    s_edit = edit; s_length = 0; s_tail_size = 0; s_text[0] = 0;
    printf("[host] Terminal attached. stdout/stderr are recorded in _fbview/session-%lu.log\n", GetCurrentProcessId());
    return 0;
}
void Fv_TerminalPump(void)
{
    unsigned char bytes[8196]; wchar_t wide[16392];
    size_t n, used; int chars, out = 0;
    if (!s_reader) return;
    fflush(stdout); fflush(stderr); clearerr(s_reader);
    memcpy(bytes, s_tail, s_tail_size);
    n = fread(bytes + s_tail_size, 1, 8192, s_reader) + s_tail_size;
    if (!n) return;
    used = n;
    /* Preserve an incomplete UTF-8 sequence until the next pump. */
    size_t start = n - 1;
    while (start > 0 && (bytes[start] & 0xc0) == 0x80 && n - start <= 3) --start;
    unsigned char lead = bytes[start];
    size_t expected = lead >= 0xf0 ? 4 : lead >= 0xe0 ? 3 : lead >= 0xc0 ? 2 : 1;
    if (expected > n - start && expected <= 4) used = start;
    s_tail_size = n - used;
    if (s_tail_size) memcpy(s_tail, bytes + used, s_tail_size);
    chars = MultiByteToWideChar(CP_UTF8, 0, (char *)bytes, (int)used, wide, 8196);
    /* EDIT expects CRLF; expand backwards through an independent buffer. */
    wchar_t normalized[16392];
    for (int i = 0; i < chars; ++i) {
        if (wide[i] == L'\n' && (i == 0 || wide[i-1] != L'\r')) normalized[out++] = L'\r';
        normalized[out++] = wide[i];
    }
    if (s_length + (size_t)out >= 65536) {
        size_t trim = s_length + (size_t)out - 50000;
        if (trim > s_length) trim = s_length;
        memmove(s_text, s_text + trim, (s_length - trim) * sizeof(wchar_t));
        s_length -= trim;
    }
    memcpy(s_text + s_length, normalized, (size_t)out * sizeof(wchar_t));
    s_length += out; s_text[s_length] = 0;
    if (s_edit) {
        SetWindowTextW(s_edit, s_text);
        SendMessageW(s_edit, EM_SETSEL, s_length, s_length);
        SendMessageW(s_edit, EM_SCROLLCARET, 0, 0);
    }
}
void Fv_TerminalStop(void)
{
    if (s_reader) Fv_TerminalPump();
    fflush(stdout); fflush(stderr);
    if (s_stdout >= 0) { _dup2(s_stdout, _fileno(stdout)); _close(s_stdout); s_stdout = -1; }
    if (s_stderr >= 0) { _dup2(s_stderr, _fileno(stderr)); _close(s_stderr); s_stderr = -1; }
    if (s_reader) { fclose(s_reader); s_reader = NULL; }
    if (s_writer) { fclose(s_writer); s_writer = NULL; }
    s_edit = NULL;
}
