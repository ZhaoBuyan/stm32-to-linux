#include "fv_terminal.c"
#include <assert.h>
int main(void)
{
    assert(Fv_TerminalStart(NULL) == 0);
    printf("adapter stdout: temperature 25 C\n");
    fprintf(stderr, "adapter stderr: test error\n");
    Fv_TerminalPump();
    assert(wcsstr(s_text, L"adapter stdout: temperature 25 C"));
    assert(wcsstr(s_text, L"adapter stderr: test error"));
    assert(wcsstr(s_text, L"\r\n"));
    Fv_TerminalStop();
    puts("PASS: terminal captures stdout/stderr and restores descriptors");
    return 0;
}
