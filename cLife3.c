/* cLife3 — 3D Life on SharkDeck framebuffer. Rule B6/S567 (Bays). */
#include <fcntl.h>
#include <linux/fb.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define N 16
#define SAVE "/home/working/clife3.box"

static uint8_t cur[N][N][N], nxt[N][N][N], prev[N][N][N];
static int gen, pop, last_pop, still, running = 1, auto_on, wrap_on = 1, rule = 1;
static int delay_ms = 800;
static int cx = 8, cy = 8, cz = 8;
static double yaw = 0.7, pitch = 0.45, zoom = 95;
static int fb = -1;
static unsigned char *map;
static size_t maplen;
static unsigned W, H, BPP, LINE;
static struct termios oldt;
static int raw_on;
static uint16_t C_BG, C_AXIS, C_TXT, C_DIM, C_HI;

static uint16_t rgb565(int r, int g, int b)
{
    if (r < 0) r = 0;
    if (g < 0) g = 0;
    if (b < 0) b = 0;
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return (uint16_t)(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
}

static void px_raw(int x, int y, uint16_t c)
{
    unsigned char *p;
    if ((unsigned)x >= W || (unsigned)y >= H)
        return;
    p = map + (size_t)y * LINE + (size_t)x * (BPP / 8);
    if (BPP == 16)
        ((uint16_t *)p)[0] = c;
    else if (BPP == 32) {
        p[0] = (unsigned char)((c & 0x1f) << 3);
        p[1] = (unsigned char)(((c >> 5) & 0x3f) << 2);
        p[2] = (unsigned char)(((c >> 11) & 0x1f) << 3);
        p[3] = 0;
    }
}

static void px(int x, int y, uint16_t c)
{
    if ((int)y < 14 || (int)y >= (int)H - 14)
        return;
    px_raw(x, y, c);
}

static void fill_rect(int x, int y, int w, int h, uint16_t c)
{
    int i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            px_raw(x + i, y + j, c);
}

static void line(int x0, int y0, int x1, int y1, uint16_t c)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        px(x0, y0, c);
        if (x0 == x1 && y0 == y1)
            break;
        {
            int e2 = 2 * err;
            if (e2 >= dy) {
                err += dy;
                x0 += sx;
            }
            if (e2 <= dx) {
                err += dx;
                y0 += sy;
            }
        }
    }
}

static const unsigned char FONT[96][5] = {
    {0,0,0,0,0},{0,0,0x5f,0,0},{0,7,0,7,0},{0x14,0x7f,0x14,0x7f,0x14},
    {0x24,0x2a,0x7f,0x2a,0x12},{0x23,0x13,8,0x64,0x62},{0x36,0x49,0x55,0x22,0x50},
    {0,5,3,0,0},{0,0x1c,0x22,0x41,0},{0,0x41,0x22,0x1c,0},{0x14,8,0x3e,8,0x14},
    {8,8,0x3e,8,8},{0,0x50,0x30,0,0},{8,8,8,8,8},{0,0x60,0x60,0,0},
    {0x20,0x10,8,4,2},{0x3e,0x51,0x49,0x45,0x3e},{0,0x42,0x7f,0x40,0},
    {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4b,0x31},{0x18,0x14,0x12,0x7f,0x10},
    {0x27,0x45,0x45,0x45,0x39},{0x3c,0x4a,0x49,0x49,0x30},{1,0x71,9,5,3},
    {0x36,0x49,0x49,0x49,0x36},{6,0x49,0x49,0x29,0x1e},{0,0x36,0x36,0,0},
    {0,0x56,0x36,0,0},{8,0x14,0x22,0x41,0},{0x14,0x14,0x14,0x14,0x14},
    {0,0x41,0x22,0x14,8},{2,1,0x51,9,6},{0x32,0x49,0x79,0x41,0x3e},
    {0x7e,0x11,0x11,0x11,0x7e},{0x7f,0x49,0x49,0x49,0x36},{0x3e,0x41,0x41,0x41,0x22},
    {0x7f,0x41,0x41,0x22,0x1c},{0x7f,0x49,0x49,0x49,0x41},{0x7f,9,9,9,1},
    {0x3e,0x41,0x49,0x49,0x7a},{0x7f,8,8,8,0x7f},{0,0x41,0x7f,0x41,0},
    {0x20,0x40,0x41,0x3f,1},{0x7f,8,0x14,0x22,0x41},{0x7f,0x40,0x40,0x40,0x40},
    {0x7f,2,0x0c,2,0x7f},{0x7f,4,8,0x10,0x7f},{0x3e,0x41,0x41,0x41,0x3e},
    {0x7f,9,9,9,6},{0x3e,0x41,0x51,0x21,0x5e},{0x7f,9,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31},{1,1,0x7f,1,1},{0x3f,0x40,0x40,0x40,0x3f},
    {0x1f,0x20,0x40,0x20,0x1f},{0x3f,0x40,0x38,0x40,0x3f},{0x63,0x14,8,0x14,0x63},
    {7,8,0x70,8,7},{0x61,0x51,0x49,0x45,0x43},{0,0x7f,0x41,0x41,0},
    {2,4,8,0x10,0x20},{0,0x41,0x41,0x7f,0},{4,2,1,2,4},{0x40,0x40,0x40,0x40,0x40},
    {0,1,2,4,0},{0x20,0x54,0x54,0x54,0x78},{0x7f,0x48,0x44,0x44,0x38},
    {0x38,0x44,0x44,0x44,0x20},{0x38,0x44,0x44,0x48,0x7f},{0x38,0x54,0x54,0x54,0x18},
    {8,0x7e,9,1,2},{0x0c,0x52,0x52,0x52,0x3e},{0x7f,8,4,4,0x78},
    {0,0x44,0x7d,0x40,0},{0x20,0x40,0x44,0x3d,0},{0x7f,0x10,0x28,0x44,0},
    {0,0x41,0x7f,0x40,0},{0x7c,4,0x18,4,0x78},{0x7c,8,4,4,0x78},
    {0x38,0x44,0x44,0x44,0x38},{0x7c,0x14,0x14,0x14,8},{8,0x14,0x14,0x18,0x7c},
    {0x7c,8,4,4,8},{0x48,0x54,0x54,0x54,0x20},{4,0x3f,0x44,0x40,0x20},
    {0x3c,0x40,0x40,0x20,0x7c},{0x1c,0x20,0x40,0x20,0x1c},{0x3c,0x40,0x30,0x40,0x3c},
    {0x44,0x28,0x10,0x28,0x44},{0x0c,0x50,0x50,0x50,0x3c},{0x44,0x64,0x54,0x4c,0x44},
};

static void text(int x, int y, const char *s, uint16_t c)
{
    while (*s) {
        unsigned char ch = (unsigned char)*s++;
        int gx, gy;
        unsigned char col;
        if (ch < 32 || ch > 126)
            ch = '?';
        for (gx = 0; gx < 5; gx++) {
            col = FONT[ch - 32][gx];
            for (gy = 0; gy < 7; gy++)
                if (col & (1 << gy))
                    px_raw(x + gx, y + gy, c);
        }
        x += 6;
    }
}

static int project(double x, double y, double z, int *sx, int *sy)
{
    double cy = cos(yaw), syw = sin(yaw), cp = cos(pitch), sp = sin(pitch);
    double x1 = x * cy - y * syw;
    double y1 = x * syw + y * cy;
    double y2 = y1 * cp - z * sp;
    double z2 = y1 * sp + z * cp;
    double f;
    if (z2 < -3.0)
        return 0;
    f = 4.0 + z2;
    if (f < 0.45)
        return 0;
    f = zoom / f;
    *sx = (int)(W * 0.50 + x1 * f);
    *sy = (int)(H * 0.48 - y2 * f);
    return 1;
}

static void edge(double x0, double y0, double z0, double x1, double y1, double z1, uint16_t c)
{
    int a, b, d, e;
    if (project(x0, y0, z0, &a, &b) && project(x1, y1, z1, &d, &e))
        line(a, b, d, e, c);
}

static int at(int x, int y, int z)
{
    if (wrap_on) {
        if (x < 0)
            x += N;
        if (y < 0)
            y += N;
        if (z < 0)
            z += N;
        if (x >= N)
            x -= N;
        if (y >= N)
            y -= N;
        if (z >= N)
            z -= N;
        return cur[x][y][z];
    }
    if (x < 0 || y < 0 || z < 0 || x >= N || y >= N || z >= N)
        return 0;
    return cur[x][y][z];
}

static int nbor(int x, int y, int z)
{
    int i, j, k, n = 0;
    for (k = -1; k <= 1; k++)
        for (j = -1; j <= 1; j++)
            for (i = -1; i <= 1; i++) {
                if (!i && !j && !k)
                    continue;
                n += at(x + i, y + j, z + k);
            }
    return n;
}

static int alive_next(int live, int nb)
{
    if (rule == 2) /* 4555: S4-5 B5 */
        return live ? (nb == 4 || nb == 5) : (nb == 5);
    if (rule == 3) /* 5766: S5-7 B6 */
        return live ? (nb >= 5 && nb <= 7) : (nb == 6);
    /* 1 B6/S567 */
    return live ? (nb >= 5 && nb <= 7) : (nb == 6);
}

static const char *rule_name(void)
{
    if (rule == 2)
        return "4555";
    if (rule == 3)
        return "5766";
    return "B6/S567";
}

static void step(void)
{
    int x, y, z;
    memcpy(prev, cur, sizeof prev);
    last_pop = pop;
    pop = 0;
    for (z = 0; z < N; z++)
        for (y = 0; y < N; y++)
            for (x = 0; x < N; x++) {
                int nb = nbor(x, y, z);
                int live = cur[x][y][z];
                /* B6 / S5-7 */
                nxt[x][y][z] = (uint8_t)alive_next(live, nb);
                pop += nxt[x][y][z];
            }
    memcpy(cur, nxt, sizeof cur);
    gen++;
    if (pop == last_pop)
        still++;
    else
        still = 0;
    if (pop == 0 || still >= 2)
        auto_on = 0;
}

static void clear_world(void)
{
    memset(cur, 0, sizeof cur);
    memset(prev, 0, sizeof prev);
    gen = pop = last_pop = still = 0;
}

static void seed_rand(int pct)
{
    int x, y, z;
    clear_world();
    for (z = 0; z < N; z++)
        for (y = 0; y < N; y++)
            for (x = 0; x < N; x++)
                if ((rand() % 100) < pct) {
                    cur[x][y][z] = 1;
                    pop++;
                }
}

static void seed_block(void)
{
    int x, y, z;
    clear_world();
    for (z = 6; z <= 9; z++)
        for (y = 6; y <= 9; y++)
            for (x = 6; x <= 9; x++) {
                cur[x][y][z] = 1;
                pop++;
            }
}

static void seed_cross(void)
{
    int i;
    clear_world();
    for (i = 4; i < 12; i++) {
        cur[i][8][8] = cur[8][i][8] = cur[8][8][i] = 1;
        pop += 3;
    }
}

static void save_box(void)
{
    FILE *f = fopen(SAVE, "wb");
    if (!f)
        return;
    fwrite("CL3\n", 1, 4, f);
    fwrite(&wrap_on, 1, sizeof wrap_on, f);
    fwrite(&rule, 1, sizeof rule, f);
    fwrite(&gen, 1, sizeof gen, f);
    fwrite(cur, 1, sizeof cur, f);
    fclose(f);
}

static void load_box(void)
{
    FILE *f = fopen(SAVE, "rb");
    char mag[4];
    if (!f)
        return;
    if (fread(mag, 1, 4, f) != 4 || memcmp(mag, "CL3\n", 4)) {
        fclose(f);
        return;
    }
    fread(&wrap_on, 1, sizeof wrap_on, f);
    fread(&rule, 1, sizeof rule, f);
    fread(&gen, 1, sizeof gen, f);
    fread(cur, 1, sizeof cur, f);
    fclose(f);
    {
        int x, y, z;
        pop = 0;
        for (z = 0; z < N; z++)
            for (y = 0; y < N; y++)
                for (x = 0; x < N; x++)
                    pop += cur[x][y][z];
    }
}

static uint16_t cellcol(int nb)
{
    if (nb <= 4)
        return rgb565(40, 160, 80);
    if (nb <= 6)
        return rgb565(80, 220, 120);
    if (nb <= 8)
        return rgb565(220, 220, 80);
    return rgb565(220, 90, 40);
}

static void blob(int sx, int sy, uint16_t c)
{
    px(sx, sy, c);
    px(sx + 1, sy, c);
    px(sx, sy + 1, c);
    px(sx + 1, sy + 1, c);
}

typedef struct {
    int x, y, z;
    double d;
} Cell;

static int cmp_cell(const void *a, const void *b)
{
    const Cell *p = a, *q = b;
    if (p->d < q->d)
        return -1;
    if (p->d > q->d)
        return 1;
    return 0;
}

static void world_xyz(int x, int y, int z, double *wx, double *wy, double *wz)
{
    double s = 2.4 / (N - 1);
    *wx = (x - (N - 1) * 0.5) * s;
    *wy = (y - (N - 1) * 0.5) * s;
    *wz = (z - (N - 1) * 0.5) * s;
}

static void render(void)
{
    int x, y, z, n = 0, i;
    char bar[80];
    Cell cells[N * N * N];
    double wx, wy, wz;
    fill_rect(0, 0, (int)W, (int)H, C_BG);
    fill_rect(0, 0, (int)W, 14, C_DIM);
    fill_rect(0, (int)H - 14, (int)W, 14, C_DIM);
    edge(-1.3, -1.3, -1.3, 1.3, -1.3, -1.3, C_AXIS);
    edge(-1.3, -1.3, -1.3, -1.3, 1.3, -1.3, C_AXIS);
    edge(-1.3, -1.3, -1.3, -1.3, -1.3, 1.3, C_AXIS);
    edge(1.3, 1.3, 1.3, -1.3, 1.3, 1.3, C_AXIS);
    edge(1.3, 1.3, 1.3, 1.3, -1.3, 1.3, C_AXIS);
    edge(1.3, 1.3, 1.3, 1.3, 1.3, -1.3, C_AXIS);
    for (z = 0; z < N; z++)
        for (y = 0; y < N; y++)
            for (x = 0; x < N; x++)
                if (cur[x][y][z]) {
                    int sx, sy;
                    world_xyz(x, y, z, &wx, &wy, &wz);
                    if (!project(wx, wy, wz, &sx, &sy))
                        continue;
                    cells[n].x = x;
                    cells[n].y = y;
                    cells[n].z = z;
                    cells[n].d = wy * sin(pitch) + wz * cos(pitch);
                    n++;
                }
    for (z = 0; z < N; z++)
        for (y = 0; y < N; y++)
            for (x = 0; x < N; x++)
                if (prev[x][y][z] && !cur[x][y][z]) {
                    int sx, sy;
                    world_xyz(x, y, z, &wx, &wy, &wz);
                    if (project(wx, wy, wz, &sx, &sy))
                        px(sx, sy, rgb565(40, 55, 50));
                }
    qsort(cells, (size_t)n, sizeof(Cell), cmp_cell);
    for (i = 0; i < n; i++)
        {
            int sx, sy;
            world_xyz(cells[i].x, cells[i].y, cells[i].z, &wx, &wy, &wz);
            if (project(wx, wy, wz, &sx, &sy))
                blob(sx, sy, cellcol(nbor(cells[i].x, cells[i].y, cells[i].z)));
        }
    world_xyz(cx, cy, cz, &wx, &wy, &wz);
    {
        int sx, sy;
        if (project(wx, wy, wz, &sx, &sy)) {
            uint16_t c = rgb565(255, 220, 40);
            line(sx - 4, sy, sx + 4, sy, c);
            line(sx, sy - 4, sx, sy + 4, c);
        }
    }
    snprintf(bar, sizeof bar, "cLife3 %s %s  g%d p%d  %dms%s", rule_name(),
             wrap_on ? "torus" : "wall", gen, pop, delay_ms, auto_on ? "*" : "");
    text(4, 4, bar, C_HI);
    snprintf(bar, sizeof bar, "cur %d %d %d n%d %s", cx, cy, cz, nbor(cx, cy, cz),
             cur[cx][cy][cz] ? "on" : "off");
    text(4, (int)H - 10, bar, C_TXT);
}

static void raw(int on)
{
    struct termios t;
    if (on) {
        tcgetattr(0, &oldt);
        t = oldt;
        t.c_lflag &= ~(ICANON | ECHO);
        t.c_cc[VMIN] = 0;
        t.c_cc[VTIME] = 0;
        tcsetattr(0, TCSANOW, &t);
        raw_on = 1;
    } else if (raw_on) {
        tcsetattr(0, TCSANOW, &oldt);
        raw_on = 0;
    }
}

static int fb_open(void)
{
    struct fb_var_screeninfo v;
    struct fb_fix_screeninfo f;
    fb = open("/dev/fb0", O_RDWR);
    if (fb < 0)
        return -1;
    if (ioctl(fb, FBIOGET_VSCREENINFO, &v) < 0)
        return -1;
    if (ioctl(fb, FBIOGET_FSCREENINFO, &f) < 0)
        return -1;
    W = v.xres;
    H = v.yres;
    BPP = v.bits_per_pixel;
    LINE = f.line_length;
    maplen = f.smem_len ? f.smem_len : (size_t)LINE * H;
    map = mmap(NULL, maplen, PROT_READ | PROT_WRITE, MAP_SHARED, fb, 0);
    return map == MAP_FAILED ? -1 : 0;
}

int main(void)
{
    if (fb_open() < 0) {
        fprintf(stderr, "cLife3: /dev/fb0\n");
        return 1;
    }
    C_BG = rgb565(6, 8, 10);
    C_AXIS = rgb565(36, 44, 52);
    C_TXT = rgb565(180, 200, 190);
    C_DIM = rgb565(16, 20, 24);
    C_HI = rgb565(80, 230, 120);
    srand((unsigned)time(NULL));
    seed_rand(7);
    raw(1);
    render();
    while (running) {
        unsigned char ch = 0;
        fd_set rf;
        struct timeval tv;
        long us = auto_on ? (long)delay_ms * 1000L : 40000;
        tv.tv_sec = us / 1000000L;
        tv.tv_usec = us % 1000000L;
        FD_ZERO(&rf);
        FD_SET(0, &rf);
        if (select(1, &rf, NULL, NULL, &tv) > 0)
            if (read(0, &ch, 1) != 1)
                ch = 0;
        if (!ch) {
            if (auto_on) {
                step();
                render();
            }
            continue;
        }
        if (ch == 0x1b) {
            unsigned char seq[8] = {0};
            struct timeval t2 = {0, 80000};
            FD_ZERO(&rf);
            FD_SET(0, &rf);
            if (select(1, &rf, NULL, NULL, &t2) > 0)
                read(0, seq, 6);
            if (seq[0] == '[' && seq[1] == 'A')
                pitch -= 0.10;
            else if (seq[0] == '[' && seq[1] == 'B')
                pitch += 0.10;
            else if (seq[0] == '[' && seq[1] == 'C')
                yaw += 0.12;
            else if (seq[0] == '[' && seq[1] == 'D')
                yaw -= 0.12;
            if (pitch > 1.2)
                pitch = 1.2;
            if (pitch < -0.2)
                pitch = -0.2;
            render();
            continue;
        }
        if (ch == 'q')
            running = 0;
        else if (ch == ' ') {
            step();
            render();
        } else if (ch == 'a') {
            auto_on ^= 1;
            render();
        } else if (ch == 'r') {
            seed_rand(7);
            render();
        } else if (ch == 'b') {
            seed_block();
            render();
        } else if (ch == 'x') {
            seed_cross();
            render();
        } else if (ch == 'c') {
            clear_world();
            render();
        } else if (ch == 'w') {
            wrap_on ^= 1;
            render();
        } else if (ch == '1' || ch == '2' || ch == '3') {
            rule = ch - '0';
            render();
        } else if (ch == 's') {
            save_box();
            render();
        } else if (ch == 'o') {
            load_box();
            render();
        } else if (ch == 'h') {
            if (cx > 0)
                cx--;
            render();
        } else if (ch == 'l') {
            if (cx < N - 1)
                cx++;
            render();
        } else if (ch == 'j') {
            if (cy > 0)
                cy--;
            render();
        } else if (ch == 'k') {
            if (cy < N - 1)
                cy++;
            render();
        } else if (ch == 'u') {
            if (cz < N - 1)
                cz++;
            render();
        } else if (ch == 'n') {
            if (cz > 0)
                cz--;
            render();
        } else if (ch == '.') {
            cur[cx][cy][cz] ^= 1;
            pop += cur[cx][cy][cz] ? 1 : -1;
            render();
        } else if (ch == '+' || ch == '=') {
            zoom *= 1.12;
            render();
        } else if (ch == '-') {
            zoom /= 1.12;
            render();
        } else if (ch >= '5' && ch <= '8') {
            seed_rand(ch - '0');
            render();
        } else if (ch == '[') {
            delay_ms += 200;
            if (delay_ms > 4000)
                delay_ms = 4000;
            render();
        } else if (ch == ']') {
            delay_ms -= 200;
            if (delay_ms < 200)
                delay_ms = 200;
            render();
        }
    }
    raw(0);
    munmap(map, maplen);
    close(fb);
    return 0;
}
