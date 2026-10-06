#include <windows.h>
#include "resource.h"

static char     szClass[] = "DemoClass";
static HINSTANCE hInstance;

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
    RECT    rc;
    HBRUSH  br, oldbr;
    int     i;

    GetClientRect(hwnd, &rc);
    for (i = 0; i < 8; i++) {
        br = CreateSolidBrush(RGB((i & 1) * 255, (i & 2) * 127, (i & 4) * 63));
        oldbr = SelectObject(hdc, br);
        Ellipse(hdc, 10 + i * 20, 10 + i * 8, 70 + i * 20, 50 + i * 8);
        SelectObject(hdc, oldbr);
        DeleteObject(br);
    }
    SetBkMode(hdc, TRANSPARENT);
    rc.top = rc.bottom - 30;
    DrawText(hdc, "VERSION 3 - hot reloaded", -1, &rc, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
}

LRESULT CALLBACK _export WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    PAINTSTRUCT ps;
    FARPROC     proc;

    switch (msg) {
    case WM_PAINT:
        Paint(hwnd, BeginPaint(hwnd, &ps));
        EndPaint(hwnd, &ps);
        return 0;
    case WM_LBUTTONDOWN:
        PostMessage(hwnd, WM_COMMAND, IDM_ABOUT, 0);
        return 0;
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
        wc.style         = CS_HREDRAW | CS_VREDRAW;
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

    hwnd = CreateWindow(szClass, "Demo", WS_OVERLAPPEDWINDOW,
                        CW_USEDEFAULT, CW_USEDEFAULT, 360, 240,
                        NULL, NULL, hInst, NULL);
    ShowWindow(hwnd, nShow);
    UpdateWindow(hwnd);

    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return msg.wParam;
}
