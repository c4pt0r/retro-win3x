/*
 * TUI demo — text-mode UI writing straight to VGA memory (B800:0000).
 * Menu bar with clock, file list window, info dialog, quit confirmation.
 *
 * Keys: Up/Down/PgUp/PgDn/Home/End move, Enter = details, F1 = about,
 *       Esc / Alt-X = quit.
 */
#include <conio.h>
#include <dos.h>
#include <i86.h>
#include <string.h>
#include <stdio.h>

#define COLS 80
#define ROWS 25

/* colors: (bg << 4) | fg */
#define C_DESK   0x17
#define C_BAR    0x70
#define C_BARKEY 0x74
#define C_WIN    0x1F
#define C_WTITLE 0x1E
#define C_SEL    0x30
#define C_DLG    0x70
#define C_BTN    0x2F
#define C_BTNOFF 0x87
#define C_SHADOW 0x08

/* extended key codes (getch() returns 0 or 0xE0, then these) */
#define K_UP    0x148
#define K_DOWN  0x150
#define K_PGUP  0x149
#define K_PGDN  0x151
#define K_HOME  0x147
#define K_END   0x14F
#define K_LEFT  0x14B
#define K_RIGHT 0x14D
#define K_F1    0x13B
#define K_ALTX  0x12D
#define K_ENTER 13
#define K_ESC   27
#define K_TAB   9

static unsigned short far *vram = MK_FP(0xB800, 0);

static const char *files[] = {
    "AUTOEXEC.BAT", "CONFIG.SYS", "COMMAND.COM", "HIMEM.SYS", "SMARTDRV.EXE",
    "EDIT.COM", "QBASIC.EXE", "MEM.EXE", "SCANDISK.EXE", "DEFRAG.EXE",
    "MSD.EXE", "KEYB.COM", "MODE.COM", "XCOPY.EXE", "DOSKEY.COM",
    "EMM386.EXE", "MSCDEX.EXE", "FDISK.EXE", "FORMAT.COM", "SYS.COM",
    "TUI.EXE", "SNAKE.EXE", "WIN.COM", "PROGMAN.EXE"
};
static const unsigned long sizes[] = {
    179, 205, 54869, 29136, 45145, 413, 194309, 39818, 124262, 75033,
    158470, 15796, 23521, 16930, 5883, 125495, 25377, 29336, 22974, 9432,
    9000, 42000, 50904, 115312
};
#define NFILES (sizeof(files) / sizeof(files[0]))

static void put(int x, int y, int ch, int attr)
{
    if (x >= 0 && x < COLS && y >= 0 && y < ROWS)
        vram[y * COLS + x] = (unsigned short)((attr << 8) | (unsigned char)ch);
}

static void text(int x, int y, const char *s, int attr)
{
    while (*s)
        put(x++, y, *s++, attr);
}

static void fill(int x, int y, int w, int h, int ch, int attr)
{
    int i, j;

    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            put(x + i, y + j, ch, attr);
}

static void shadow(int x, int y, int w, int h)
{
    int i;

    for (i = 1; i <= h; i++) {
        unsigned short far *p = &vram[(y + i) * COLS + x + w];
        p[0] = (p[0] & 0xFF) | (C_SHADOW << 8);
        p[1] = (p[1] & 0xFF) | (C_SHADOW << 8);
    }
    for (i = 2; i < w + 2; i++) {
        unsigned short far *p = &vram[(y + h) * COLS + x + i];
        *p = (*p & 0xFF) | (C_SHADOW << 8);
    }
}

/* double-line box with centered title */
static void window(int x, int y, int w, int h, const char *title, int attr)
{
    int i;

    fill(x, y, w, h, ' ', attr);
    put(x, y, 0xC9, attr);
    put(x + w - 1, y, 0xBB, attr);
    put(x, y + h - 1, 0xC8, attr);
    put(x + w - 1, y + h - 1, 0xBC, attr);
    for (i = 1; i < w - 1; i++) {
        put(x + i, y, 0xCD, attr);
        put(x + i, y + h - 1, 0xCD, attr);
    }
    for (i = 1; i < h - 1; i++) {
        put(x, y + i, 0xBA, attr);
        put(x + w - 1, y + i, 0xBA, attr);
    }
    if (title) {
        int tx = x + (w - (int)strlen(title) - 2) / 2;

        put(tx, y, ' ', attr);
        text(tx + 1, y, title, (attr & 0xF0) | 0x0E);
        put(tx + 1 + strlen(title), y, ' ', attr);
    }
    shadow(x, y, w, h);
}

static void cursor(int on)
{
    union REGS r;

    r.h.ah = 1;
    r.w.cx = on ? 0x0607 : 0x2000;
    int86(0x10, &r, &r);
}

static int key(void)
{
    int c = getch();

    if (c == 0 || c == 0xE0)
        return 0x100 | getch();
    return c;
}

static void showclock(void)
{
    struct dostime_t t;
    char buf[12];

    _dos_gettime(&t);
    sprintf(buf, " %02d:%02d:%02d ", t.hour, t.minute, t.second);
    text(COLS - 11, 0, buf, C_BAR);
}

/* wait for a key, keeping the clock ticking */
static int waitkey(void)
{
    while (!kbhit()) {
        showclock();
        delay(50);
    }
    return key();
}

static void desktop(void)
{
    fill(0, 1, COLS, ROWS - 2, 0xB1, C_DESK);
    fill(0, 0, COLS, 1, ' ', C_BAR);
    text(2, 0, "File", C_BAR);  put(2, 0, 'F', C_BARKEY);
    text(9, 0, "View", C_BAR);  put(9, 0, 'V', C_BARKEY);
    text(16, 0, "Help", C_BAR); put(16, 0, 'H', C_BARKEY);
    fill(0, ROWS - 1, COLS, 1, ' ', C_BAR);
    text(1, ROWS - 1, "F1", C_BARKEY);  text(4, ROWS - 1, "About", C_BAR);
    text(11, ROWS - 1, "\x18\x19", C_BARKEY); text(14, ROWS - 1, "Move", C_BAR);
    text(20, ROWS - 1, "Enter", C_BARKEY); text(26, ROWS - 1, "Details", C_BAR);
    text(35, ROWS - 1, "Esc", C_BARKEY); text(39, ROWS - 1, "Quit", C_BAR);
    text(56, ROWS - 1, "Open Watcom / DOS", C_BAR);
    showclock();
}

#define LX 4
#define LY 3
#define LW 40
#define LH 19
#define VISIBLE (LH - 2)

static void filelist(int sel, int top)
{
    int i;
    char buf[64];

    window(LX, LY, LW, LH, "C:\\DOS", C_WIN);
    for (i = 0; i < VISIBLE; i++) {
        int n = top + i;
        int attr = (n == sel) ? C_SEL : C_WIN;

        if (n >= (int)NFILES)
            break;
        sprintf(buf, " %-14s %10lu bytes ", files[n], sizes[n]);
        fill(LX + 1, LY + 1 + i, LW - 2, 1, ' ', attr);
        text(LX + 2, LY + 1 + i, buf, attr);
    }
    /* scrollbar */
    for (i = 0; i < VISIBLE; i++)
        put(LX + LW - 1, LY + 1 + i, 0xB0, C_WIN);
    put(LX + LW - 1, LY + 1 + (sel * (VISIBLE - 1)) / (NFILES - 1), 0xDB, C_WIN);
}

static void infopanel(int sel)
{
    char buf[40];
    unsigned long total = 0;
    int i;

    for (i = 0; i < (int)NFILES; i++)
        total += sizes[i];
    window(48, 3, 28, 9, "Info", C_WIN);
    sprintf(buf, "File  %s", files[sel]);        text(50, 5, buf, C_WIN);
    sprintf(buf, "Size  %lu", sizes[sel]);       text(50, 6, buf, C_WIN);
    sprintf(buf, "Item  %d of %d", sel + 1, (int)NFILES); text(50, 7, buf, C_WIN);
    sprintf(buf, "Total %lu", total);            text(50, 9, buf, C_WIN);

    window(48, 14, 28, 8, "Memory", C_WIN);
    {
        unsigned kb = *(unsigned short far *)MK_FP(0x40, 0x13);
        int bar = kb * 22 / 640;

        sprintf(buf, "Conventional %u KB", kb);  text(50, 16, buf, C_WIN);
        fill(50, 18, 22, 1, 0xB0, C_WIN);
        fill(50, 18, bar, 1, 0xDB, 0x1A);
    }
}

/* modal message box; buttons is "OK" or "Yes|No"; returns button index */
static int dialog(const char *title, const char *l1, const char *l2, int yesno)
{
    int w = 44, h = 8, x = (COLS - w) / 2, y = 8, sel = 0, k;

    for (;;) {
        window(x, y, w, h, title, C_DLG);
        text(x + 3, y + 2, l1, C_DLG);
        if (l2)
            text(x + 3, y + 3, l2, C_DLG);
        if (yesno) {
            text(x + 11, y + 5, "  Yes  ", sel == 0 ? C_BTN : C_BTNOFF);
            text(x + 25, y + 5, "  No   ", sel == 1 ? C_BTN : C_BTNOFF);
        } else {
            text(x + 18, y + 5, "  OK  ", C_BTN);
        }
        k = waitkey();
        if (k == K_ENTER)
            return sel;
        if (k == K_ESC)
            return yesno ? 1 : 0;
        if (yesno && (k == K_TAB || k == K_LEFT || k == K_RIGHT))
            sel ^= 1;
        if (yesno && (k == 'y' || k == 'Y'))
            return 0;
        if (yesno && (k == 'n' || k == 'N'))
            return 1;
    }
}

int main(void)
{
    int sel = 0, top = 0, k;
    char buf[48];

    cursor(0);
    for (;;) {
        if (sel < top)
            top = sel;
        if (sel >= top + VISIBLE)
            top = sel - VISIBLE + 1;
        desktop();
        filelist(sel, top);
        infopanel(sel);

        k = waitkey();
        switch (k) {
        case K_UP:   if (sel > 0) sel--; break;
        case K_DOWN: if (sel < (int)NFILES - 1) sel++; break;
        case K_PGUP: sel = sel > VISIBLE ? sel - VISIBLE : 0; break;
        case K_PGDN: sel = sel + VISIBLE < (int)NFILES ? sel + VISIBLE : NFILES - 1; break;
        case K_HOME: sel = 0; break;
        case K_END:  sel = NFILES - 1; break;
        case K_ENTER:
            sprintf(buf, "%s is %lu bytes.", files[sel], sizes[sel]);
            dialog("Details", buf, "(this is only a demo)", 0);
            break;
        case K_F1:
            dialog("About", "TUI demo - 16-bit real mode DOS",
                   "Cross-compiled on Linux, Open Watcom", 0);
            break;
        case K_ESC:
        case K_ALTX:
            if (dialog("Quit", "Exit the TUI demo?", NULL, 1) == 0) {
                fill(0, 0, COLS, ROWS, ' ', 0x07);
                cursor(1);
                return 0;
            }
            break;
        }
    }
}
