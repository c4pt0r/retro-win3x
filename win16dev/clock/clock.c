#include <windows.h>
#include <stdio.h>
#include <time.h>
#include "resource.h"

static char      szClass[] = "ClockClass";
static HINSTANCE hInstance;
static HFONT     hBig, hSmall;

BOOL CALLBACK _export AboutProc(HWND hdlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    (void)lParam;
    switch (msg) {
    case WM_INITDIALOG:
        return TRUE;
    case WM_COMMAND:
        if (wParam == IDOK || wParam == IDCANCEL) {
            EndDialog(hdlg, 0);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static void Paint(HWND hwnd, HDC hdc)
{
    static const char *wd[] = { "星期日", "星期一", "星期二", "星期三",
                                "星期四", "星期五", "星期六" };
    struct tm   *tm;
    time_t       now;
    RECT         rc;
    HFONT        old;
    char         buf[80];

    now = time(NULL);
    tm  = localtime(&now);
    GetClientRect(hwnd, &rc);
    SetBkMode(hdc, TRANSPARENT);

    old = SelectObject(hdc, hBig);
    sprintf(buf, "%02d:%02d:%02d", tm->tm_hour, tm->tm_min, tm->tm_sec);
    rc.bottom = rc.top + 64;
    DrawText(hdc, buf, -1, &rc, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

    SelectObject(hdc, hSmall);
    sprintf(buf, "%d年%d月%d日 %s", tm->tm_year + 1900, tm->tm_mon + 1,
            tm->tm_mday, wd[tm->tm_wday & 7]);
    rc.top = rc.bottom + 8;
    rc.bottom = rc.top + 20;
    DrawText(hdc, buf, -1, &rc, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

    SelectObject(hdc, old);
}

LRESULT CALLBACK _export WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    PAINTSTRUCT ps;
    FARPROC     proc;

    switch (msg) {
    case WM_CREATE:
        hBig = CreateFont(-48, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                          CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                          DEFAULT_PITCH, "宋体");
        hSmall = CreateFont(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                            CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                            DEFAULT_PITCH, "宋体");
        SetTimer(hwnd, 1, 1000, NULL);
        return 0;
    case WM_TIMER:
        InvalidateRect(hwnd, NULL, FALSE);  /* no erase: WM_PAINT covers it all */
        return 0;
    case WM_ERASEBKGND:
        return 1;                           /* background is drawn off-screen */
    case WM_PAINT: {
        /* double buffer: draw the whole frame into a memory bitmap, then
           copy it to the screen in one BitBlt, so nothing flickers */
        HDC     hdc = BeginPaint(hwnd, &ps), mem;
        HBITMAP bmp, oldbmp;
        HBRUSH  br;
        RECT    rc;

        GetClientRect(hwnd, &rc);
        mem = CreateCompatibleDC(hdc);
        bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);  /* screen DC, not mem */
        oldbmp = SelectObject(mem, bmp);
        /* Win16 FillRect needs a real brush: the (HBRUSH)(COLOR_xxx + 1)
           shortcut only works for a window class's hbrBackground */
        br = CreateSolidBrush(GetSysColor(COLOR_WINDOW));
        FillRect(mem, &rc, br);
        DeleteObject(br);
        SetTextColor(mem, GetSysColor(COLOR_WINDOWTEXT));
        Paint(hwnd, mem);
        BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, oldbmp);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_COMMAND:
        switch (wParam) {
        case IDM_ABOUT:
            proc = MakeProcInstance((FARPROC)AboutProc, hInstance);
            DialogBox(hInstance, MAKEINTRESOURCE(IDD_ABOUT), hwnd, (DLGPROC)proc);
            FreeProcInstance(proc);
            return 0;
        case IDM_EXIT:
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        if (hBig)   DeleteObject(hBig);
        if (hSmall) DeleteObject(hSmall);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int PASCAL WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow)
{
    WNDCLASS wc;
    HWND     hwnd;
    MSG      msg;

    (void)lpCmd;
    hInstance = hInst;
    if (!hPrev) {
        wc.style         = 0;
        wc.lpfnWndProc   = WndProc;
        wc.cbClsExtra    = 0;
        wc.cbWndExtra    = 0;
        wc.hInstance     = hInst;
        wc.hIcon         = LoadIcon(hInst, MAKEINTRESOURCE(IDI_APP));
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszMenuName  = MAKEINTRESOURCE(IDM_MAIN);
        wc.lpszClassName = szClass;
        if (!RegisterClass(&wc))
            return 0;
    }

    hwnd = CreateWindow(szClass, "时钟",
                        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                        CW_USEDEFAULT, CW_USEDEFAULT, 360, 190,
                        NULL, NULL, hInst, NULL);
    ShowWindow(hwnd, nShow);
    UpdateWindow(hwnd);

    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return msg.wParam;
}
