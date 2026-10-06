/*
 * SNAKE — 320x200x256 (mode 13h) demo, 32-bit protected mode (DOS/4GW).
 *
 * Starts in attract mode: the computer plays. Press an arrow key to take
 * over; Esc quits. Back buffer + vsync, custom palette, BIOS ROM 8x8 font,
 * PC speaker blips.
 */
#include <conio.h>
#include <i86.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#define W       320
#define H       200
#define CELL    8
#define GW      (W / CELL)          /* 40 */
#define GH      (H / CELL - 2)      /* 23: two rows reserved for the HUD */
#define GY0     (2 * CELL)          /* playfield top in pixels */
#define MAXLEN  (GW * GH)

/* palette indices */
#define P_BG      0                 /* 0..31  background gradient */
#define P_SNAKE   32                /* 32..63 snake gradient head->tail */
#define P_FOOD    64                /* 64..71 pulsing food */
#define P_WHITE   80
#define P_HUD     81
#define P_GRID    82
#define P_SHADOW  83
#define P_RED     84

static unsigned char *vga = (unsigned char *)0xA0000;
static unsigned char *font = (unsigned char *)0xFFA6E;  /* BIOS 8x8, chars 0-127 */
static unsigned char buf[W * H];

static int sx[MAXLEN], sy[MAXLEN], len, dir, fx, fy, score, best, attract;
static int dx[4] = { 1, 0, -1, 0 }, dy[4] = { 0, 1, 0, -1 };  /* R D L U */
static int beep;
static unsigned long frame;

static void setmode(int mode)
{
    union REGS r;

    r.w.ax = mode;
    int386(0x10, &r, &r);
}

static void setpal(int i, int r, int g, int b)
{
    outp(0x3C8, i);
    outp(0x3C9, r);
    outp(0x3C9, g);
    outp(0x3C9, b);
}

static void palette(void)
{
    int i;

    for (i = 0; i < 32; i++)
        setpal(P_BG + i, 0, i / 4, 8 + i / 2);         /* navy -> teal */
    for (i = 0; i < 32; i++)
        setpal(P_SNAKE + i, 63 - i, 63, 20 + i);       /* yellow-green -> cyan */
    setpal(P_WHITE, 63, 63, 63);
    setpal(P_HUD, 10, 10, 25);
    setpal(P_GRID, 4, 8, 18);
    setpal(P_SHADOW, 0, 2, 6);
    setpal(P_RED, 63, 15, 15);
}

static void vsync(void)
{
    while (inp(0x3DA) & 8)
        ;
    while (!(inp(0x3DA) & 8))
        ;
}

static void rect(int x, int y, int w, int h, int c)
{
    int j;

    for (j = 0; j < h; j++)
        memset(buf + (y + j) * W + x, c, w);
}

static void ch(int x, int y, int c, int color)
{
    int i, j;
    unsigned char *g = font + (c & 127) * 8;

    for (j = 0; j < 8; j++)
        for (i = 0; i < 8; i++)
            if (g[j] & (0x80 >> i))
                buf[(y + j) * W + x + i] = color;
}

static void str(int x, int y, const char *s, int color)
{
    for (; *s; s++, x += 8) {
        ch(x + 1, y + 1, *s, P_SHADOW);
        ch(x, y, *s, color);
    }
}

static void center(int y, const char *s, int color)
{
    str((W - 8 * (int)strlen(s)) / 2, y, s, color);
}

static int occupied(int x, int y, int skiptail)
{
    int i;

    if (x < 0 || y < 0 || x >= GW || y >= GH)
        return 1;
    for (i = 0; i < len - skiptail; i++)
        if (sx[i] == x && sy[i] == y)
            return 1;
    return 0;
}

static void placefood(void)
{
    do {
        fx = rand() % GW;
        fy = rand() % GH;
    } while (occupied(fx, fy, 0));
}

static void reset(void)
{
    int i;

    len = 5;
    dir = 0;
    for (i = 0; i < len; i++) {
        sx[i] = GW / 2 - i;
        sy[i] = GH / 2;
    }
    score = 0;
    placefood();
}

/* free cells reachable from (x,y) — cheap flood fill, capped */
static unsigned char seen[GW * GH];
static int stack[GW * GH];
static int space(int x, int y)
{
    int n = 0, sp = 0, i;

    memset(seen, 0, sizeof(seen));
    for (i = 0; i < len - 1; i++)
        seen[sy[i] * GW + sx[i]] = 1;
    if (occupied(x, y, 1))
        return 0;
    stack[sp++] = y * GW + x;
    seen[y * GW + x] = 1;
    while (sp && n < 200) {
        int p = stack[--sp], px = p % GW, py = p / GW, d;

        n++;
        for (d = 0; d < 4; d++) {
            int nx = px + dx[d], ny = py + dy[d];

            if (nx >= 0 && ny >= 0 && nx < GW && ny < GH && !seen[ny * GW + nx]) {
                seen[ny * GW + nx] = 1;
                stack[sp++] = ny * GW + nx;
            }
        }
    }
    return n;
}

/* attract-mode AI: prefer moves toward food that leave room to live */
static void think(void)
{
    int d, bestd = dir, bestscore = -1000000;

    for (d = 0; d < 4; d++) {
        int nx = sx[0] + dx[d], ny = sy[0] + dy[d], s;

        if (d == (dir + 2) % 4 || occupied(nx, ny, 1))
            continue;
        s = space(nx, ny) * 4 - (abs(nx - fx) + abs(ny - fy));
        if (space(nx, ny) < len)
            s -= 1000;
        if (s > bestscore) {
            bestscore = s;
            bestd = d;
        }
    }
    dir = bestd;
}

/* returns 0 on death */
static int step(void)
{
    int nx = sx[0] + dx[dir], ny = sy[0] + dy[dir], grow = (nx == fx && ny == fy);

    if (occupied(nx, ny, !grow))
        return 0;
    if (grow && len < MAXLEN)
        len++;
    memmove(sx + 1, sx, (len - 1) * sizeof(int));
    memmove(sy + 1, sy, (len - 1) * sizeof(int));
    sx[0] = nx;
    sy[0] = ny;
    if (grow) {
        score += 10;
        if (score > best)
            best = score;
        placefood();
        sound(880 + (score % 100) * 8);
        beep = 3;
    }
    return 1;
}

static void draw(const char *banner)
{
    int x, y, i;
    char s[48];

    /* background gradient + subtle grid */
    for (y = GY0; y < H; y++)
        memset(buf + y * W, P_BG + (y - GY0) * 31 / (H - GY0), W);
    for (y = GY0; y < H; y += CELL)
        memset(buf + y * W, P_GRID, W);
    for (x = 0; x < W; x += CELL)
        for (y = GY0; y < H; y++)
            buf[y * W + x] = P_GRID;

    /* food, pulsing */
    i = (int)(frame / 4) % 3;
    setpal(P_FOOD, 63, 20 + i * 15, 10);
    rect(fx * CELL + 2 - i / 2, GY0 + fy * CELL + 2 - i / 2, 5 + i, 5 + i, P_FOOD);

    /* snake, tail to head */
    for (i = len - 1; i >= 0; i--) {
        int c = P_SNAKE + (len > 1 ? i * 31 / (len - 1) : 0);

        rect(sx[i] * CELL + 2, GY0 + sy[i] * CELL + 2, CELL - 1, CELL - 1, P_SHADOW);
        rect(sx[i] * CELL + 1, GY0 + sy[i] * CELL + 1, CELL - 1, CELL - 1, c);
    }
    /* eyes */
    x = sx[0] * CELL + 1;
    y = GY0 + sy[0] * CELL + 1;
    buf[(y + 2) * W + x + 2] = 0;
    buf[(y + 2) * W + x + 4] = 0;

    /* HUD */
    rect(0, 0, W, GY0, P_HUD);
    sprintf(s, "SCORE %05d", score);
    str(8, 4, s, P_WHITE);
    sprintf(s, "BEST %05d", best);
    str(W - 8 - 8 * (int)strlen(s), 4, s, P_WHITE);
    if (attract && (frame / 30) % 2)
        center(4, "DEMO", P_RED);

    if (banner) {
        rect(40, 80, 240, 48, P_HUD);
        center(88, banner, P_RED);
        center(108, attract ? "Arrow keys: play   Esc: quit" : "Press any key", P_WHITE);
    } else if (attract) {
        center(H - 12, "ARROW KEYS TO PLAY - ESC TO QUIT", P_WHITE);
    }

    vsync();
    memcpy(vga, buf, sizeof(buf));
    frame++;
    if (beep && !--beep)
        nosound();
}

/* returns direction 0-3, -1 none, -2 escape, -3 other key */
static int readkey(void)
{
    int c;

    if (!kbhit())
        return -1;
    c = getch();
    if (c == 27)
        return -2;
    if (c == 0 || c == 0xE0) {
        switch (getch()) {
        case 0x4D: return 0;
        case 0x50: return 1;
        case 0x4B: return 2;
        case 0x48: return 3;
        }
    }
    return -3;
}

int main(void)
{
    int k, t, speed;

    srand((unsigned)time(NULL));
    setmode(0x13);
    palette();
    attract = 1;
    reset();

    for (;;) {
        speed = attract ? 3 : (len < 20 ? 6 : len < 40 ? 5 : 4);
        for (t = 0; t < speed; t++) {
            k = readkey();
            if (k == -2)
                goto out;
            if (k >= 0) {
                if (attract) {          /* player takes over */
                    attract = 0;
                    reset();
                }
                if (k != (dir + 2) % 4)
                    dir = k;
            }
            draw(NULL);
        }
        if (attract)
            think();
        if (!step()) {
            nosound();
            sound(110);
            for (t = 0; t < 70; t++) {
                draw("GAME OVER");
                if (t == 10)
                    nosound();
                if (!attract && t > 20 && (k = readkey()) != -1) {
                    if (k == -2)
                        goto out;
                    break;
                }
            }
            nosound();
            attract = 1;
            reset();
        }
    }
out:
    nosound();
    setmode(3);
    printf("SNAKE: best score %d\n", best);
    return 0;
}
