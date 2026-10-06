/* EXITWIN — exit Windows to DOS (one-shot helper) */
#include <windows.h>

int PASCAL WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow)
{
    (void)hInst; (void)hPrev; (void)lpCmd; (void)nShow;
    ExitWindows(0, 0);
    return 0;
}
