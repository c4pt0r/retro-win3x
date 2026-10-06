/*
 * W16AGENT — resident helper for driving Windows 3.1 from Linux.
 *
 * Listens on COM1 (QEMU: -serial unix:com1.sock) for one command per line
 * and answers with one line starting "OK" or "ERR":
 *
 *   PING                    -> OK pong
 *   RUN A:\APP.EXE [args]   close running APP, copy it to C:\W16RUN, start it
 *   EXEC cmdline            WinExec as-is
 *   CLOSE module            post WM_CLOSE to the module's windows, wait for exit
 *   TASKS                   list running module names
 *   COPY src dst            copy a file (e.g. A:\X.EXE C:\W16RUN\X.EXE)
 *   INI file section key value   WritePrivateProfileString
 *   EXIT                    exit Windows to DOS
 *   RESTART                 exit and restart Windows (AUTOEXEC.BAT loop)
 *   UPDATE A:\W16AGENT.EXE  install a new agent, then RESTART
 *   VERSION                 agent version
 *   DIR pattern             list files: "OK n" then "F name size" lines, "END"
 *   DEL path / MKDIR path   delete a file / create a directory
 *   PUT path size           receive a file (see transfer protocol below)
 *   GET path                send a file
 *
 * Transfer protocol (data is base64, max 240 raw bytes per line):
 *   PUT:  > PUT path size   < OK ready
 *         > D <b64>         < K            (repeat)
 *         > END <crc32>     < OK size crc32 | ERR ...
 *   GET:  > GET path        < OK size
 *         < D <b64>         > K            (repeat; reply X to abort)
 *         < END <crc32>
 *
 * Started from WIN.INI "load=" line; runs minimized. RESTART/UPDATE rely on
 * this loop at the end of AUTOEXEC.BAT:
 *
 *   :W16LOOP
 *   IF EXIST C:\W16RUN\W16AGENT.NEW COPY C:\W16RUN\W16AGENT.NEW C:\W16RUN\W16AGENT.EXE
 *   IF EXIST C:\W16RUN\W16AGENT.NEW DEL C:\W16RUN\W16AGENT.NEW
 *   IF EXIST C:\W16RUN\RESTART.FLG DEL C:\W16RUN\RESTART.FLG
 *   WIN
 *   IF EXIST C:\W16RUN\RESTART.FLG GOTO W16LOOP
 */
#include <windows.h>
#include <string.h>
#include <direct.h>
#include <stdio.h>
#include <stdlib.h>
#include <dos.h>

#define RUNDIR  "C:\\W16RUN"
#define LINEMAX 512
#define CHUNK   240
#define VERSION "3"

static HINSTANCE hInstance;
static int       cid = -1;
static char      line[LINEMAX];     /* current command */
static char      xline[LINEMAX];    /* transfer lines */
static char      rx[LINEMAX];       /* bytes received, not yet a full line */
static int       rxlen;
static char      baud[8];
static char      szTarget[9];
static char      reply[512];

static void Pump(DWORD ms);

static void Write(const char *s, int n)
{
    COMSTAT st;
    DWORD   end = GetTickCount() + 5000;

    while (n > 0 && GetTickCount() < end) {
        int w = WriteComm(cid, (LPSTR)s, n);

        if (w < 0) {
            GetCommError(cid, &st);
            w = -w;
        }
        s += w;
        n -= w;
        if (n)
            Pump(10);
    }
}

static void Send(const char *s)
{
    Write(s, lstrlen(s));
    Write("\r\n", 2);
}

/* Read one line (without CR/LF) into out. Waits up to timeout ms
   (0 = only what is already buffered). Returns length or -1. */
static int GetLine(char *out, DWORD timeout)
{
    DWORD   end = GetTickCount() + timeout;
    COMSTAT st;
    int     i, n;

    for (;;) {
        for (i = 0; i < rxlen; i++) {
            if (rx[i] == '\r' || rx[i] == '\n') {
                int len = i;

                memcpy(out, rx, len);
                out[len] = '\0';
                while (i < rxlen && (rx[i] == '\r' || rx[i] == '\n'))
                    i++;
                memmove(rx, rx + i, rxlen - i);
                rxlen -= i;
                if (len)
                    return len;
                i = -1;             /* blank line: rescan */
            }
        }
        if (rxlen == LINEMAX - 1)   /* overlong line: drop it */
            rxlen = 0;
        n = ReadComm(cid, rx + rxlen, LINEMAX - 1 - rxlen);
        if (n < 0) {
            GetCommError(cid, &st);
            n = -n;
        }
        rxlen += n;
        if (n)
            continue;
        if (GetTickCount() >= end)
            return -1;
        Pump(5);
    }
}

static void ModuleOf(HWND hwnd, char *name)
{
    char path[144], *base, *dot;

    name[0] = '\0';
    if (!GetModuleFileName((HINSTANCE)GetWindowWord(hwnd, GWW_HINSTANCE), path, sizeof(path)))
        return;
    base = strrchr(path, '\\');
    base = base ? base + 1 : path;
    dot = strchr(base, '.');
    if (dot)
        *dot = '\0';
    lstrcpyn(name, base, 9);
}

BOOL CALLBACK _export CloseEnum(HWND hwnd, LPARAM lParam)
{
    char name[9];

    (void)lParam;
    ModuleOf(hwnd, name);
    if (lstrcmpi(name, szTarget) == 0)
        PostMessage(hwnd, WM_CLOSE, 0, 0);
    return TRUE;
}

BOOL CALLBACK _export TaskEnum(HWND hwnd, LPARAM lParam)
{
    char name[9];

    (void)lParam;
    if (!IsWindowVisible(hwnd) && !IsIconic(hwnd))
        return TRUE;
    ModuleOf(hwnd, name);
    if (name[0] && lstrlen(reply) + 10 < sizeof(reply)) {
        char key[12];

        wsprintf(key, " %s", (LPSTR)name);
        if (!strstr(reply, key))
            lstrcat(reply, key);
    }
    return TRUE;
}

static void Pump(DWORD ms)
{
    DWORD end = GetTickCount() + ms;
    MSG   msg;

    while (GetTickCount() < end) {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        Yield();
    }
}

/* returns TRUE if module is no longer loaded */
static BOOL CloseModule(const char *module)
{
    FARPROC proc;
    int     i;

    lstrcpyn(szTarget, module, sizeof(szTarget));
    if (!GetModuleHandle(szTarget))
        return TRUE;
    proc = MakeProcInstance((FARPROC)CloseEnum, hInstance);
    for (i = 0; i < 50 && GetModuleHandle(szTarget); i++) {
        if (i % 10 == 0)
            EnumWindows((WNDENUMPROC)proc, 0);
        Pump(100);
    }
    FreeProcInstance(proc);
    return GetModuleHandle(szTarget) == NULL;
}

static BOOL CopyFile(const char *src, const char *dst)
{
    static char buf[4096];
    HFILE in, out;
    UINT  n;
    BOOL  ok = TRUE;

    if ((in = _lopen(src, OF_READ)) == HFILE_ERROR)
        return FALSE;
    if ((out = _lcreat(dst, 0)) == HFILE_ERROR) {
        _lclose(in);
        return FALSE;
    }
    while ((n = _lread(in, buf, sizeof(buf))) > 0 && n != (UINT)HFILE_ERROR)
        if (_lwrite(out, buf, n) != n) {
            ok = FALSE;
            break;
        }
    if (n == (UINT)HFILE_ERROR)
        ok = FALSE;
    _lclose(in);
    _lclose(out);
    return ok;
}

static void Exec(const char *cmd)
{
    UINT rc = WinExec(cmd, SW_SHOWNORMAL);

    if (rc < 32)
        wsprintf(reply, "ERR WinExec %u: %s", rc, (LPSTR)cmd);
    else
        wsprintf(reply, "OK started %s", (LPSTR)cmd);
}

static void Run(char *arg)
{
    char  src[128], dst[160], cmd[256], module[9], *args, *base, *dot;

    lstrcpyn(src, arg, sizeof(src));
    args = strchr(src, ' ');
    if (args)
        *args++ = '\0';
    base = strrchr(src, '\\');
    base = base ? base + 1 : (strchr(src, ':') ? strchr(src, ':') + 1 : src);
    lstrcpyn(module, base, sizeof(module));
    dot = strchr(module, '.');
    if (dot)
        *dot = '\0';

    if (!CloseModule(module)) {
        wsprintf(reply, "ERR %s still running", (LPSTR)module);
        return;
    }
    mkdir(RUNDIR);
    wsprintf(dst, RUNDIR "\\%s", (LPSTR)base);
    if (!CopyFile(src, dst)) {
        wsprintf(reply, "ERR copy %s -> %s failed", (LPSTR)src, (LPSTR)dst);
        return;
    }
    wsprintf(cmd, "%s %s", (LPSTR)dst, (LPSTR)(args ? args : ""));
    Exec(cmd);
}

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

static int B64Val(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

/* returns decoded length or -1 on bad input */
static int B64Decode(const char *in, unsigned char *out)
{
    unsigned long v = 0;
    int bits = 0, n = 0, d;

    for (; *in && *in != '='; in++) {
        if ((d = B64Val(*in)) < 0)
            return -1;
        v = (v << 6) | d;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out[n++] = (unsigned char)(v >> bits);
        }
    }
    return n;
}

static unsigned long Crc32(unsigned long crc, const unsigned char *p, int n)
{
    int k;

    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        for (k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320UL & (0 - (crc & 1)));
    }
    return ~crc;
}

static void Put(char *path, long size)
{
    static unsigned char data[CHUNK + 4];
    unsigned long crc = 0, want;
    long  got = 0;
    HFILE f;
    int   n;

    if ((f = _lcreat(path, 0)) == HFILE_ERROR) {
        wsprintf(reply, "ERR cannot create %s", (LPSTR)path);
        Send(reply);
        return;
    }
    Send("OK ready");
    for (;;) {
        if (GetLine(xline, 10000) < 0) {
            lstrcpy(reply, "ERR timeout");
            break;
        }
        if (xline[0] == 'D' && xline[1] == ' ') {
            n = B64Decode(xline + 2, data);
            if (n < 0 || n > CHUNK || got + n > size) {
                lstrcpy(reply, "ERR bad data line");
                break;
            }
            if (_lwrite(f, data, n) != (UINT)n) {
                lstrcpy(reply, "ERR write failed (disk full?)");
                break;
            }
            crc = Crc32(crc, data, n);
            got += n;
            Send("K");
        } else if (!strncmp(xline, "END ", 4)) {
            want = strtoul(xline + 4, NULL, 16);
            if (got != size)
                wsprintf(reply, "ERR got %ld of %ld bytes", got, size);
            else if (want != crc)
                wsprintf(reply, "ERR crc %08lx, expected %08lx", crc, want);
            else
                wsprintf(reply, "OK %ld %08lx", got, crc);
            break;
        } else {
            lstrcpy(reply, "ERR transfer aborted");
            break;
        }
    }
    _lclose(f);
    if (reply[0] == 'E')
        remove(path);
    Send(reply);
}

static void Get(char *path)
{
    static unsigned char data[CHUNK];
    unsigned long crc = 0;
    long  size;
    HFILE f;
    int   n;

    if ((f = _lopen(path, OF_READ)) == HFILE_ERROR) {
        wsprintf(reply, "ERR cannot open %s", (LPSTR)path);
        Send(reply);
        return;
    }
    size = _llseek(f, 0, 2);
    _llseek(f, 0, 0);
    wsprintf(reply, "OK %ld", size);
    Send(reply);
    while ((n = _lread(f, data, CHUNK)) > 0 && n != (int)HFILE_ERROR) {
        crc = Crc32(crc, data, n);
        xline[0] = 'D';
        xline[1] = ' ';
        B64Encode(data, n, xline + 2);
        Send(xline);
        if (GetLine(xline, 10000) < 0 || xline[0] != 'K') {
            _lclose(f);
            return;                 /* aborted / timed out: no END */
        }
    }
    _lclose(f);
    wsprintf(reply, "END %08lx", crc);
    Send(reply);
}

static void Dir(char *pattern)
{
    struct find_t ff;
    int  n = 0;
    char buf[64];

    if (!pattern[0])
        pattern = RUNDIR "\\*.*";
    if (_dos_findfirst(pattern, _A_NORMAL | _A_RDONLY | _A_SUBDIR | _A_ARCH, &ff) == 0) {
        do {
            n++;
        } while (_dos_findnext(&ff) == 0);
    }
    wsprintf(reply, "OK %d", n);
    Send(reply);
    if (n && _dos_findfirst(pattern, _A_NORMAL | _A_RDONLY | _A_SUBDIR | _A_ARCH, &ff) == 0) {
        do {
            if (ff.attrib & _A_SUBDIR)
                wsprintf(buf, "F %s <DIR>", (LPSTR)ff.name);
            else
                wsprintf(buf, "F %s %ld", (LPSTR)ff.name, ff.size);
            Send(buf);
        } while (_dos_findnext(&ff) == 0);
    }
    Send("END");
}

static void Restart(void)
{
    HFILE f;

    mkdir(RUNDIR);
    f = _lcreat(RUNDIR "\\RESTART.FLG", 0);
    if (f != HFILE_ERROR)
        _lclose(f);
    Send("OK restarting");
    if (!ExitWindows(0, 0)) {
        remove(RUNDIR "\\RESTART.FLG");
        Send("ERR exit refused by an application");
    }
}

/* split "a b rest" into at most n fields (last one keeps spaces) */
static int Split(char *s, char **f, int n)
{
    int i = 0;

    while (i < n) {
        while (*s == ' ')
            s++;
        if (!*s)
            break;
        f[i++] = s;
        if (i == n)
            break;
        while (*s && *s != ' ')
            s++;
        if (*s)
            *s++ = '\0';
    }
    return i;
}

static void Command(char *s)
{
    char *arg, *f[4];
    FARPROC proc;

    while (*s == ' ')
        s++;
    arg = strchr(s, ' ');
    if (arg) {
        *arg++ = '\0';
        while (*arg == ' ')
            arg++;
    } else {
        arg = "";
    }
    AnsiUpper(s);

    if (!s[0])
        return;
    if (!lstrcmp(s, "PING")) {
        lstrcpy(reply, "OK pong");
    } else if (!lstrcmp(s, "RUN") && arg[0]) {
        Run(arg);
    } else if (!lstrcmp(s, "EXEC") && arg[0]) {
        Exec(arg);
    } else if (!lstrcmp(s, "CLOSE") && arg[0]) {
        if (CloseModule(arg))
            wsprintf(reply, "OK closed %s", (LPSTR)arg);
        else
            wsprintf(reply, "ERR %s still running", (LPSTR)arg);
    } else if (!lstrcmp(s, "TASKS")) {
        lstrcpy(reply, "OK");
        proc = MakeProcInstance((FARPROC)TaskEnum, hInstance);
        EnumWindows((WNDENUMPROC)proc, 0);
        FreeProcInstance(proc);
    } else if (!lstrcmp(s, "COPY") && Split(arg, f, 2) == 2) {
        if (CopyFile(f[0], f[1]))
            wsprintf(reply, "OK copied %s -> %s", (LPSTR)f[0], (LPSTR)f[1]);
        else
            wsprintf(reply, "ERR copy %s -> %s failed", (LPSTR)f[0], (LPSTR)f[1]);
    } else if (!lstrcmp(s, "INI") && Split(arg, f, 4) == 4) {
        if (WritePrivateProfileString(f[1], f[2], f[3], f[0]))
            wsprintf(reply, "OK [%s] %s=%s in %s", (LPSTR)f[1], (LPSTR)f[2], (LPSTR)f[3], (LPSTR)f[0]);
        else
            lstrcpy(reply, "ERR WritePrivateProfileString failed");
    } else if (!lstrcmp(s, "EXIT")) {
        Send("OK exiting");
        if (!ExitWindows(0, 0))
            Send("ERR exit refused by an application");
        return;
    } else if (!lstrcmp(s, "RESTART")) {
        Restart();
        return;
    } else if (!lstrcmp(s, "UPDATE") && arg[0]) {
        mkdir(RUNDIR);
        if (!CopyFile(arg, RUNDIR "\\W16AGENT.NEW")) {
            wsprintf(reply, "ERR copy %s failed", (LPSTR)arg);
        } else {
            Restart();
            return;
        }
    } else if (!lstrcmp(s, "VERSION")) {
        wsprintf(reply, "OK w16agent " VERSION " baud %s", (LPSTR)baud);
    } else if (!lstrcmp(s, "PUT") && Split(arg, f, 2) == 2) {
        Put(f[0], atol(f[1]));
        return;
    } else if (!lstrcmp(s, "GET") && arg[0]) {
        Get(arg);
        return;
    } else if (!lstrcmp(s, "DIR")) {
        Dir(arg);
        return;
    } else if (!lstrcmp(s, "DEL") && arg[0]) {
        if (remove(arg) == 0)
            wsprintf(reply, "OK deleted %s", (LPSTR)arg);
        else
            wsprintf(reply, "ERR cannot delete %s", (LPSTR)arg);
    } else if (!lstrcmp(s, "MKDIR") && arg[0]) {
        if (mkdir(arg) == 0)
            wsprintf(reply, "OK created %s", (LPSTR)arg);
        else
            wsprintf(reply, "ERR cannot create %s", (LPSTR)arg);
    } else {
        wsprintf(reply, "ERR unknown command %s", (LPSTR)s);
    }
    Send(reply);
}

static void Poll(HWND hwnd)
{
    KillTimer(hwnd, 1);
    while (GetLine(line, 0) > 0)
        Command(line);
    SetTimer(hwnd, 1, 100, NULL);
}

LRESULT CALLBACK _export WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_TIMER:
        Poll(hwnd);
        return 0;
    case WM_QUERYOPEN:          /* stay an icon */
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        if (cid >= 0)
            CloseComm(cid);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int PASCAL WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow)
{
    WNDCLASS wc;
    DCB      dcb;
    HWND     hwnd;
    MSG      msg;

    (void)lpCmd;
    (void)nShow;
    if (hPrev)
        return 0;
    hInstance = hInst;
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);

    cid = OpenComm("COM1", 4096, 4096);
    if (cid < 0) {
        MessageBox(NULL, "cannot open COM1", "W16AGENT", MB_OK | MB_ICONSTOP);
        return 1;
    }
    BuildCommDCB("COM1:9600,n,8,1", &dcb);
    dcb.BaudRate = CBR_56000;               /* QEMU paces TX by baud rate */
    lstrcpy(baud, "57600");
    if (SetCommState(&dcb) < 0) {
        dcb.BaudRate = CBR_19200;
        lstrcpy(baud, "19200");
        if (SetCommState(&dcb) < 0)
            lstrcpy(baud, "9600");
    }

    wc.style         = 0;
    wc.lpfnWndProc   = WndProc;
    wc.cbClsExtra    = 0;
    wc.cbWndExtra    = 0;
    wc.hInstance     = hInst;
    wc.hIcon         = LoadIcon(NULL, IDI_ASTERISK);
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName  = NULL;
    wc.lpszClassName = "W16Agent";
    RegisterClass(&wc);

    hwnd = CreateWindow("W16Agent", "W16 Agent", WS_OVERLAPPEDWINDOW,
                        CW_USEDEFAULT, CW_USEDEFAULT, 200, 100,
                        NULL, NULL, hInst, NULL);
    ShowWindow(hwnd, SW_SHOWMINNOACTIVE);
    SetTimer(hwnd, 1, 100, NULL);
    Send("OK agent started");

    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}
