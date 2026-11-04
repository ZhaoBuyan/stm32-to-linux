#ifndef FV_TERMINAL_H
#define FV_TERMINAL_H
#include <windows.h>
/* Captures native stdout/stderr into a per-session log and mirrors it to EDIT. */
int Fv_TerminalStart(HWND edit);
void Fv_TerminalPump(void);
void Fv_TerminalStop(void);
#endif
