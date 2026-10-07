/*
 * CHAT — "LLM" chat window for Windows 3.x that talks to the pi coding agent on the
 * Linux host through COM2 (QEMU: -serial unix:com2.sock; host side: w16chatd).
 *
 * Enter sends, Ctrl+Enter inserts a line break. Prev/Next switches sessions.
 * Text on the wire is GBK, base64-encoded, one command per line (see w16chatd).
 *
 * Replies arrive as GBK, so Chinese shows up fine on Chinese Windows.
 */
#include <windows.h>
#include <string.h>
#include <stdlib.h>
#include "resource.h"

#define PORT        "COM2"
#define INMAX       1024            /* max message length (bytes) */
#define RXMAX       2048
#define LOGMAX      24000           /* trim the transcript beyond this */
#define LOGTRIM     8000

static HINSTANCE hInst;
static HWND      hwndMain, hwndLog, hwndInput, hwndSend, hwndNew, hwndSessions, hwndStatus;
static HWND      hwndContext;
static FARPROC   lpfnOldLog, lpfnOldInput, lpfnEdit;
static int       cid = -1;
static BOOL      busy;
static int       lineH;
static char      rx[RXMAX];
static int       rxlen;
static char      agent[32] = "LLM";
static char      model[64] = "?";
static char      sessionInfo[16] = "1/1";
static char      pend[RXMAX];       /* reply text collected during one Poll() */
static int       pendlen;
static char      status[128];
static HBRUSH    hbrFace;
static HFONT     hfontChat;

/* ---- base64 ---------------------------------------------------------- */

static const char b64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int B64Encode(const unsigned char *in, int n, char *out)
{
    char *o = out;
    int   i;

    for (i = 0; i < n; i += 3) {
        unsigned long v = (unsigned long)in[i] << 16;

        if (i + 1 < n) v |= (unsigned)in[i + 1] << 8;
        if (i + 2 < n) v |= in[i + 2];
        *o++ = b64[(v >> 18) & 63];
        *o++ = b64[(v >> 12) & 63];
        *o++ = i + 1 < n ? b64[(v >> 6) & 63] : '=';
        *o++ = i + 2 < n ? b64[v & 63] : '=';
    }
    *o = '\0';
    return o - out;
}

static int B64Decode(const char *in, char *out)
{
    unsigned long v = 0;
    int bits = 0, n = 0, d;

    for (; *in && *in != '='; in++) {
        char c = *in;

        if (c >= 'A' && c <= 'Z') d = c - 'A';
        else if (c >= 'a' && c <= 'z') d = c - 'a' + 26;
        else if (c >= '0' && c <= '9') d = c - '0' + 52;
        else if (c == '+') d = 62;
        else if (c == '/') d = 63;
        else continue;
        v = (v << 6) | d;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out[n++] = (char)(v >> bits);
        }
    }
    out[n] = '\0';
    return n;
}

/* ---- serial ---------------------------------------------------------- */

static void SendLine(const char *s)
{
    COMSTAT st;
    int     n = lstrlen(s), w;
    DWORD   end = GetTickCount() + 3000;

    while (n > 0 && GetTickCount() < end) {
        w = WriteComm(cid, (LPSTR)s, n);
        if (w < 0) {
            GetCommError(cid, &st);
            w = -w;
        }
        s += w;
        n -= w;
    }
    WriteComm(cid, "\r\n", 2);
}

/* ---- transcript ------------------------------------------------------ */

static void Append(const char *s)
{
    int len = GetWindowTextLength(hwndLog);

    if (len > LOGMAX) {
        /* cut whole lines from the top, so no double-byte char is split */
        int line = (int)SendMessage(hwndLog, EM_LINEFROMCHAR, LOGTRIM, 0L);
        int cut = (int)SendMessage(hwndLog, EM_LINEINDEX, line + 1, 0L);

        if (cut > 0) {
            /* deleting the top scrolls everything: repaint once, not twice */
            SendMessage(hwndLog, WM_SETREDRAW, FALSE, 0L);
            SendMessage(hwndLog, EM_SETSEL, 0, MAKELONG(0, cut));
            SendMessage(hwndLog, EM_REPLACESEL, 0, (LPARAM)(LPSTR)"");
            SendMessage(hwndLog, WM_SETREDRAW, TRUE, 0L);
            InvalidateRect(hwndLog, NULL, TRUE);
            len = GetWindowTextLength(hwndLog);
        }
    }
    SendMessage(hwndLog, EM_SETSEL, 0, MAKELONG(len, len));
    SendMessage(hwndLog, EM_REPLACESEL, 0, (LPARAM)(LPSTR)s);
}

/* Reply text is queued and inserted once per timer tick: one EM_REPLACESEL
   (one repaint) instead of one per serial line. */
static void Flush(void)
{
    if (pendlen) {
        pend[pendlen] = '\0';
        pendlen = 0;
        Append(pend);
    }
}

static void Queue(const char *s)
{
    int n = lstrlen(s);

    if (pendlen + n >= RXMAX - 1)
        Flush();
    if (n >= RXMAX - 1) {
        Append(s);
        return;
    }
    memcpy(pend + pendlen, s, n);
    pendlen += n;
}

static void Status(const char *s)
{
    if (lstrcmp(s, status) == 0)        /* avoid repainting the same text */
        return;
    lstrcpyn(status, s, sizeof(status));
    SetWindowText(hwndStatus, s);
}

static void SetBusy(BOOL b)
{
    busy = b;
    SetWindowText(hwndSend, b ? "Stop" : "Send");
}

static void UpdateTitle(void)
{
    char title[160];

    wsprintf(title, "LLM - %s - %s [%s]",
             (LPSTR)agent, (LPSTR)model, (LPSTR)sessionInfo);
    SetWindowText(hwndMain, title);
}

static void SetSessionPosition(char *s)
{
    char *slash = strchr(s, '/');
    int current = 1, total = 1;

    if (slash) {
        *slash++ = '\0';
        current = atoi(s);
        total = atoi(slash);
    }
    if (current < 1) current = 1;
    if (total < 1) total = 1;
    wsprintf(sessionInfo, "%d/%d", current, total);
    UpdateTitle();
    SendMessage(hwndSessions, LB_SETCURSEL, current - 1, 0L);
}

static void SetSessionList(char *s)
{
    char *p = s, *start;

    SendMessage(hwndSessions, LB_RESETCONTENT, 0, 0L);
    while (*p) {
        start = p;
        while (*p && *p != '\r' && *p != '\n')
            p++;
        if (*p) {
            *p++ = '\0';
            while (*p == '\r' || *p == '\n')
                p++;
        }
        if (*start)
            SendMessage(hwndSessions, LB_ADDSTRING, 0, (LPARAM)(LPSTR)start);
    }
}

static void DoSend(void)
{
    static char text[INMAX + 1];
    static char line[INMAX * 4 / 3 + 16];
    int n;

    if (busy) {                         /* button is "Stop" */
        SendLine("STOP");
        Status("Stopping...");
        return;
    }
    n = GetWindowText(hwndInput, text, sizeof(text));
    if (n == 0)
        return;
    Flush();
    if (GetWindowTextLength(hwndLog) > 0)
        Append("\r\n");
    Append("Me: ");
    Append(text);
    Append("\r\n");
    lstrcpy(line, "MSG ");
    B64Encode((unsigned char *)text, n, line + 4);
    SendLine(line);
    SetWindowText(hwndInput, "");
    SetBusy(TRUE);
    Status("Sent, waiting for reply...");
    SetFocus(hwndInput);
}

static void HandleLine(char *s)
{
    static char text[RXMAX];

    switch (s[0]) {
    case 'B':
        Queue("\r\n");
        Queue(agent);
        Queue(": ");
        break;
    case 'T': {                         /* EDIT needs CR LF: fix bare LF */
        static char fixed[RXMAX * 2];
        char *p, *o = fixed;

        B64Decode(s + 2, text);
        for (p = text; *p; p++) {
            if (*p == '\n' && (p == text || p[-1] != '\r'))
                *o++ = '\r';
            *o++ = *p;
        }
        *o = '\0';
        Queue(fixed);
        break;
    }
    case 'E':
        if (busy)
            Queue("\r\n");
        SetBusy(FALSE);
        break;
    case 'C':                           /* clear transcript before session replay */
        pendlen = 0;
        SetWindowText(hwndLog, "");
        SetBusy(FALSE);
        SetFocus(hwndInput);
        break;
    case 'L':                           /* newline-separated session labels */
        B64Decode(s + 2, text);
        SetSessionList(text);
        break;
    case 'I': {                         /* "agent|model" -> window title */
        char *bar;

        B64Decode(s + 2, text);
        bar = strchr(text, '|');
        if (bar)
            *bar++ = '\0';
        lstrcpyn(agent, text, sizeof(agent));
        lstrcpyn(model, bar ? bar : "?", sizeof(model));
        UpdateTitle();
        break;
    }
    case 'V':                           /* current/total session number */
        B64Decode(s + 2, text);
        SetSessionPosition(text);
        break;
    case 'S':
        B64Decode(s + 2, text);
        Status(text);
        break;
    case 'X':
        B64Decode(s + 2, text);
        Queue("\r\n[Error] ");
        Queue(text);
        Queue("\r\n");
        Status(text);
        break;
    }
}

static void Poll(void)
{
    COMSTAT st;
    int     n, i, start;

    for (;;) {
        n = ReadComm(cid, rx + rxlen, RXMAX - 1 - rxlen);
        if (n < 0) {
            GetCommError(cid, &st);
            n = -n;
        }
        if (n == 0)
            break;
        rxlen += n;
        start = 0;
        for (i = 0; i < rxlen; i++) {
            if (rx[i] == '\r' || rx[i] == '\n') {
                rx[i] = '\0';
                if (i > start)
                    HandleLine(rx + start);
                start = i + 1;
            }
        }
        memmove(rx, rx + start, rxlen - start);
        rxlen -= start;
        if (rxlen >= RXMAX - 1)         /* garbage without newline */
            rxlen = 0;
    }
    Flush();
}

/* ---- edit context menu: copy/paste ----------------------------------- */

static void ShowEditMenu(HWND hwndEdit, LPARAM lParam)
{
    HMENU menu = CreatePopupMenu();
    DWORD sel;
    UINT copyFlags = MF_STRING;
    UINT pasteFlags = MF_STRING;
    POINT pt;

    if (!menu)
        return;
    sel = (DWORD)SendMessage(hwndEdit, EM_GETSEL, 0, 0L);
    if (LOWORD(sel) == HIWORD(sel))
        copyFlags |= MF_GRAYED;
    if (GetDlgCtrlID(hwndEdit) != IDC_INPUT)
        pasteFlags |= MF_GRAYED;
    AppendMenu(menu, copyFlags, IDM_COPY, "复制");
    AppendMenu(menu, pasteFlags, IDM_PASTE, "粘贴");

    pt.x = (short)LOWORD(lParam);
    pt.y = (short)HIWORD(lParam);
    ClientToScreen(hwndEdit, &pt);
    SetFocus(hwndEdit);
    hwndContext = hwndEdit;
    TrackPopupMenu(menu, TPM_LEFTALIGN, pt.x, pt.y, 0, hwndMain, NULL);
    DestroyMenu(menu);
}

/* Enter sends; Ctrl+Enter inserts a line break. Right-click opens Copy/Paste. */
LRESULT CALLBACK _export EditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    FARPROC oldProc = hwnd == hwndInput ? lpfnOldInput : lpfnOldLog;

    if (msg == WM_RBUTTONUP) {
        ShowEditMenu(hwnd, lParam);
        return 0;
    }
    if (hwnd == hwndInput) {
        if (msg == WM_KEYDOWN && wParam == VK_RETURN) {
            if (GetKeyState(VK_CONTROL) < 0)
                SendMessage(hwnd, EM_REPLACESEL, 0, (LPARAM)(LPSTR)"\r\n");
            else
                PostMessage(hwndMain, WM_COMMAND, IDC_SEND, 0L);
            return 0;
        }
        if (msg == WM_CHAR && (wParam == '\r' || wParam == '\n'))
            return 0;
    }
    return CallWindowProc(oldProc, hwnd, msg, wParam, lParam);
}

/* ---- main window ----------------------------------------------------- */

static void Layout(int cx, int cy)
{
    int m = 6, bw = 72, bh = lineH + 10, sw = 132;
    int sh = lineH + 4;                 /* status line */
    int ih = 2 * bh + m;                /* input box = two buttons tall */
    int iy = cy - m - sh - m - ih;
    int rightX = m + sw + m;

    /* "New" sits flush with the bottom edge of the input box */
    MoveWindow(hwndSessions, m, m, sw, iy + ih - bh - 2 * m, TRUE);
    MoveWindow(hwndNew, m, iy + ih - bh, sw, bh, TRUE);
    MoveWindow(hwndLog, rightX, m, cx - rightX - m, iy - 2 * m, TRUE);
    MoveWindow(hwndInput, rightX, iy, cx - rightX - 3 * m - bw, ih, TRUE);
    MoveWindow(hwndSend, cx - m - bw, iy, bw, bh, TRUE);
    MoveWindow(hwndStatus, m, cy - m - sh, cx - 2 * m, sh, TRUE);
}

static void Create(HWND hwnd)
{
    TEXTMETRIC tm;
    HDC        hdc = GetDC(hwnd);
    HFONT      hfontOld;
    DCB        dcb;

    /* Use Windows 3.2's built-in raster System font for crisp text. */
    hfontChat = (HFONT)GetStockObject(SYSTEM_FONT);
    hfontOld = (HFONT)SelectObject(hdc, hfontChat);
    GetTextMetrics(hdc, &tm);
    SelectObject(hdc, hfontOld);
    ReleaseDC(hwnd, hdc);
    lineH = tm.tmHeight;

    hwndLog = CreateWindow("EDIT", "", WS_CHILD | WS_VISIBLE | WS_BORDER |
                           WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
                           0, 0, 0, 0, hwnd, (HMENU)IDC_LOG, hInst, NULL);
    hwndInput = CreateWindow("EDIT", "", WS_CHILD | WS_VISIBLE | WS_BORDER |
                             WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL,
                             0, 0, 0, 0, hwnd, (HMENU)IDC_INPUT, hInst, NULL);
    hwndSend = CreateWindow("BUTTON", "Send", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                            0, 0, 0, 0, hwnd, (HMENU)IDC_SEND, hInst, NULL);
    hwndSessions = CreateWindow("LISTBOX", "", WS_CHILD | WS_VISIBLE | WS_BORDER |
                                WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
                                0, 0, 0, 0, hwnd, (HMENU)IDC_SESSIONS, hInst, NULL);
    hwndNew = CreateWindow("BUTTON", "新建", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                           0, 0, 0, 0, hwnd, (HMENU)IDC_NEW, hInst, NULL);
    hwndStatus = CreateWindow("STATIC", "", WS_CHILD | WS_VISIBLE | SS_LEFT,
                              0, 0, 0, 0, hwnd, (HMENU)IDC_STATUS, hInst, NULL);
    if (hfontChat) {
        SendMessage(hwndLog, WM_SETFONT, (WPARAM)hfontChat, TRUE);
        SendMessage(hwndInput, WM_SETFONT, (WPARAM)hfontChat, TRUE);
        SendMessage(hwndSend, WM_SETFONT, (WPARAM)hfontChat, TRUE);
        SendMessage(hwndSessions, WM_SETFONT, (WPARAM)hfontChat, TRUE);
        SendMessage(hwndNew, WM_SETFONT, (WPARAM)hfontChat, TRUE);
        SendMessage(hwndStatus, WM_SETFONT, (WPARAM)hfontChat, TRUE);
    }
    SendMessage(hwndInput, EM_LIMITTEXT, INMAX, 0L);
    lpfnEdit = MakeProcInstance((FARPROC)EditProc, hInst);
    lpfnOldLog = (FARPROC)SetWindowLong(hwndLog, GWL_WNDPROC, (LONG)lpfnEdit);
    lpfnOldInput = (FARPROC)SetWindowLong(hwndInput, GWL_WNDPROC, (LONG)lpfnEdit);

    cid = OpenComm(PORT, RXMAX * 4, 2048);
    if (cid < 0) {
        Status("Cannot open " PORT " (VM needs -serial unix:com2.sock)");
        EnableWindow(hwndSend, FALSE);
        return;
    }
    BuildCommDCB(PORT ":9600,n,8,1", &dcb);
    dcb.BaudRate = CBR_56000;
    if (SetCommState(&dcb) < 0) {
        dcb.BaudRate = CBR_19200;
        SetCommState(&dcb);
    }
    Status("Waiting for w16chatd on the Linux host...");
    SendLine("HELLO");
    SetTimer(hwnd, 1, 50, NULL);
}

LRESULT CALLBACK _export WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE:
        hwndMain = hwnd;
        hbrFace = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
        Create(hwnd);
        return 0;
    case WM_SIZE:
        Layout(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_CTLCOLOR:                   /* status line: dialog-gray, not white */
        if ((HWND)LOWORD(lParam) == hwndStatus) {
            SetBkColor((HDC)wParam, GetSysColor(COLOR_BTNFACE));
            SetTextColor((HDC)wParam, GetSysColor(COLOR_BTNTEXT));
            return (LRESULT)hbrFace;
        }
        break;
    case WM_SYSCOLORCHANGE:
        DeleteObject(hbrFace);
        hbrFace = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
        break;
    case WM_SETFOCUS:
        SetFocus(hwndInput);
        return 0;
    case WM_TIMER:
        Poll();
        return 0;
    case WM_COMMAND:
        switch (wParam) {
        case IDM_COPY:
            if (hwndContext)
                SendMessage(hwndContext, WM_COPY, 0, 0L);
            hwndContext = NULL;
            return 0;
        case IDM_PASTE:
            if (hwndContext && GetDlgCtrlID(hwndContext) == IDC_INPUT)
                SendMessage(hwndInput, WM_PASTE, 0, 0L);
            hwndContext = NULL;
            return 0;
        case IDC_SEND:
            DoSend();
            return 0;
        case IDC_NEW:
            if (cid >= 0)
                SendLine("NEW");
            pendlen = 0;
            SetWindowText(hwndLog, "");
            SetBusy(FALSE);
            SetFocus(hwndInput);
            return 0;
        case IDC_SESSIONS:
            if (HIWORD(lParam) == LBN_SELCHANGE && cid >= 0) {
                int index = (int)SendMessage(hwndSessions, LB_GETCURSEL, 0, 0L);
                char command[32];

                if (index >= 0) {
                    wsprintf(command, "SWITCH %d", index);
                    SendLine(command);
                    Status("Switching session...");
                }
            }
            return 0;
        }
        break;
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        if (cid >= 0)
            CloseComm(cid);
        SetWindowLong(hwndLog, GWL_WNDPROC, (LONG)lpfnOldLog);
        SetWindowLong(hwndInput, GWL_WNDPROC, (LONG)lpfnOldInput);
        FreeProcInstance(lpfnEdit);
        DeleteObject(hbrFace);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int PASCAL WinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPSTR lpCmd, int nShow)
{
    WNDCLASS wc;
    MSG      msg;

    (void)lpCmd;
    if (hPrev) {                        /* one window per COM port */
        HWND w = FindWindow("PiChat", NULL);

        if (w)
            BringWindowToTop(w);
        return 0;
    }
    hInst = hInstance;
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.cbClsExtra    = 0;
    wc.cbWndExtra    = 0;
    wc.hInstance     = hInstance;
    wc.hIcon         = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_CHAT));
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszMenuName  = NULL;
    wc.lpszClassName = "PiChat";
    if (!RegisterClass(&wc))
        return 0;

    hwndMain = CreateWindow("PiChat", "LLM", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                            CW_USEDEFAULT, CW_USEDEFAULT, 560, 400,
                            NULL, NULL, hInstance, NULL);
    ShowWindow(hwndMain, nShow);
    UpdateWindow(hwndMain);

    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return msg.wParam;
}
