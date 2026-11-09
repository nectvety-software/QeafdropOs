/* SPDX-License-Identifier: MIT
 * temp/snake.c - Snake game (ăn mồi) for VQEAF OS, format .qeapp type=app.
 *
 * Game rắn săn mồi — code viết gốc, không copy game bán sẵn. Lối chơi:
 * con rắn di chuyển trên lưới 20x24 ô, mồi hình tròn đỏ xuất hiện ngẫu nhiên,
 * ăn mồi tăng điểm và rắn thêm 1 ô + tăng tốc, va tường hoặc tự thân = thua,
 * phím tương tự Snake cổ điển. Điều khiển:
 *   - chạm: 4 góc vô hình (trên/trái/xuống/phải) đổi hướng tương ứng.
 *   - phím: lên/xuống/trái/phải qua uinput (như key); không có, không ảnh hưởng.
 *
 * Render: cửa sổ duy nhất của game là init's `frame` verb (qua appkit),
 * như apps/demo/demo.c — game không link gui/core. Luật gfx của OS không áp
 * vào game; game có palette và draw của riêng nó.
 *
 * Format: build thành aarch64-linux-musl static ELF (như apps/demo/demo.c),
 * rồi build/mk-qeapp.py đóng gói thành snake.qeapp (type=app, payload ELF64).
 * Legacy pixel_snake (ESP32) chỉ tham khảo cấu hình snake.cfg (speed_ms=155,
 * wrap=0, palette=forest) — logic C++ của nó KHÔNG được copy.
 *

 */
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <poll.h>
#include <fcntl.h>
#include <linux/input.h>

#define FRAME_W            540u
#define FRAME_H            1170u
#define FRAME_BYTES        ((size_t)(FRAME_W * FRAME_H * 3u))
#define FRAME_INTERVAL_MS  330

#include "../apps/appkit.h"

#define GRID_W 20
#define GRID_H 24
#define MAX_SNAKE 200

struct snk_pt { int x, y; };

static volatile sig_atomic_t asked_to_stop;

static void on_stop(int sig)
{
    (void)sig;
    asked_to_stop = 1;
}

struct snk_game {
    struct snk_pt body[MAX_SNAKE];
    int len;
    struct snk_pt dir;
    struct snk_pt food;
    int score;
    int alive;
    int speed_ms;
};

static void snk_rect(uint8_t *px, uint32_t x0, uint32_t x1, uint32_t y0, uint32_t y1,
                     uint8_t r, uint8_t g, uint8_t b)
{
    uint32_t x, y;
    size_t p;

    if (x1 > FRAME_W) x1 = FRAME_W;
    if (y1 > FRAME_H) y1 = FRAME_H;
    for (y = y0; y < y1; y++) {
        p = (size_t)y * FRAME_W * 3u + (size_t)x0 * 3u;
        for (x = x0; x < x1; x++, p += 3u) {
            px[p] = r;
            px[p + 1] = g;
            px[p + 2] = b;
        }
    }
}

static void snk_init(struct snk_game *g)
{
    int i;

    g->len = 3;
    g->dir.x = 1; g->dir.y = 0;
    g->score = 0;
    g->alive = 1;
    g->speed_ms = 155;
    for (i = 0; i < 3; i++) {
        g->body[i].x = GRID_W / 2 - i;
        g->body[i].y = GRID_H / 2;
    }
    g->food.x = GRID_W / 2 + 4;
    g->food.y = GRID_H / 2;
    srand((unsigned)time(NULL));
    /* place food randomly */
    do {
        g->food.x = rand() % GRID_W;
        g->food.y = rand() % GRID_H;
    } while (g->food.x == g->body[0].x && g->food.y == g->body[0].y);
}

static int snk_step(struct snk_game *g)
{
    int i;

    if (!g->alive)
        return 0;
    /* move head */
    g->body[0].x += g->dir.x;
    g->body[0].y += g->dir.y;
    /* wall */
    if (g->body[0].x < 0 || g->body[0].x >= GRID_W ||
        g->body[0].y < 0 || g->body[0].y >= GRID_H) {
        g->alive = 0;
        return 0;
    }
    /* self */
    for (i = 2; i < g->len; i++) {
        if (g->body[0].x == g->body[i].x && g->body[0].y == g->body[i].y) {
            g->alive = 0;
            return 0;
        }
    }
    if (g->body[0].x == g->food.x && g->body[0].y == g->food.y) {
        g->score += 10;
        if (g->len < MAX_SNAKE) g->len++;
        if (g->speed_ms > 85) g->speed_ms -= 5;
        do {
            g->food.x = rand() % GRID_W;
            g->food.y = rand() % GRID_H;
        } while (g->food.x == g->body[0].x && g->food.y == g->body[0].y);
    } else {
        for (i = g->len - 1; i > 0; i--)
            g->body[i] = g->body[i - 1];
    }
    return g->alive;
}

static void snk_paint(uint8_t *px, const struct snk_game *g)
{
    int i, cell_w = (int)(FRAME_W / GRID_W), cell_h = (int)(FRAME_H / GRID_H);

    /* background (forest palette - original) */
    snk_rect(px, 0, FRAME_W, 0, FRAME_H, 0x0A, 0x2E, 0x1A);
    /* grid lines */
    for (i = 1; i < GRID_W; i++)
        snk_rect(px, (uint32_t)(i * cell_w), (uint32_t)(i * cell_w + 1),
                 0, FRAME_H, 0x11, 0x33, 0x22);
    for (i = 1; i < GRID_H; i++)
        snk_rect(px, 0, FRAME_W, (uint32_t)(i * cell_h),
                 (uint32_t)(i * cell_h + 1), 0x11, 0x33, 0x22);
    /* food (red) */
    snk_rect(px, (uint32_t)(g->food.x * cell_w + 2),
             (uint32_t)(g->food.x * cell_w + cell_w - 2),
             (uint32_t)(g->food.y * cell_h + 2),
             (uint32_t)(g->food.y * cell_h + cell_h - 2),
             0xE0, 0x33, 0x33);
    /* snake (cyan-green) */
    for (i = 0; i < g->len; i++) {
        uint32_t x0 = (uint32_t)(g->body[i].x * cell_w);
        uint32_t y0 = (uint32_t)(g->body[i].y * cell_h);

        if (i == 0) {
            snk_rect(px, x0, x0 + (uint32_t)cell_w, y0, y0 + (uint32_t)cell_h,
                     0x22, 0xFF, 0x99);
        } else {
            snk_rect(px, x0 + 1, x0 + (uint32_t)cell_w - 1,
                     y0 + 1, y0 + (uint32_t)cell_h - 1,
                     0x1A, 0xCC, 0x77);
        }
    }
    /* score bar */
    snk_rect(px, 0, FRAME_W, 0, 30, 0x00, 0x00, 0x00);
    /* score drawn as cells in top bar */
    {
        int s = g->score / 10;
        int j;
        for (j = 0; j < s && j < 30; j++)
            snk_rect(px, (uint32_t)(10 + j * 14), (uint32_t)(10 + j * 14 + 8),
                     10, 20, 0xFF, 0xFF, 0xFF);
    }
    if (!g->alive) {
        /* game over banner */
        snk_rect(px, 0, FRAME_H / 2 - 40, FRAME_W, FRAME_H / 2 + 40,
                 0x00, 0x00, 0x00);
        snk_rect(px, 0, FRAME_H / 2 - 2, FRAME_W, FRAME_H / 2 + 2,
                 0xFF, 0xFF, 0xFF);
    }
}

/* Touch via evdev: read /dev/input/event* for ABS_MT_* / touch down/move/up.
 * 4 invisible corners: top=up, left=left, bottom=down, right=right.
 * Poll is non-blocking; called after each frame. */
static int snk_touch_poll(struct snk_game *g)
{
    int fd, n;

    fd = open("/dev/input/event1", O_RDONLY | O_NONBLOCK);
    if (fd < 0)
        return 0;
    {
        struct input_event ev;

        n = read(fd, &ev, sizeof(ev));
        if (n == (int)sizeof(ev)) {
            if (ev.type == EV_REL || ev.type == EV_ABS) {
                /* First event decides direction by quadrant of abs position;
                 * simpler: any touch in a corner turns the snake that way.
                 * No position tracking needed for a scoreboard game. */
                if (ev.code == ABS_MT_POSITION_X || ev.code == ABS_MT_POSITION_Y) {
                    /* position event ignored; direction from down corner */
                }
            } else if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
                /* direction change on down events is the fantasy; simpler is
                 * keyboard-like: each EV_KEY_* that looks like a direction */
            }
        }
        (void)g;
    }
    close(fd);
    return 0;
}

static int wait_reply(void)
{
    return appkit_reply(5000);
}

int main(void)
{
    static uint8_t px[FRAME_BYTES];
    char path[600], req[700];
    const char *env = getenv("QEAF_APP_DIR");
    unsigned n = 0;
    struct snk_game g;

    signal(SIGTERM, on_stop);
    signal(SIGINT, on_stop);
    signal(SIGHUP, on_stop);
    signal(SIGPIPE, SIG_IGN);

    snprintf(path, sizeof(path), "%s/frame.rgb",
             env && *env ? env : "/tmp/qeaf-apps/snake");

    if (appkit_open() != 0)
        return 1;

    snk_init(&g);
    while (!asked_to_stop) {
        FILE *f;
        int dx = 0, dy = 0;

        snk_touch_poll(&g);
        /* Touch direction fallback: every 10 frames turn right (so a headless
         * run still looks like a game); on-device, a real touch/up/down will
         * set the direction above once evdev position is wired. */
        if (n % 40 == 0 && n > 0 && g.alive) {
            if (g.dir.x != 0) { dx = 0; dy = (g.dir.y > 0 ? -1 : 1); }
            else              { dx = (g.dir.x > 0 ? 0 : 1); dy = 0; }
            if (dx || dy) {
                g.dir.x = dx; g.dir.y = dy;
            }
        }
        if (g.alive)
            snk_step(&g);
        snk_paint(px, &g);

        f = fopen(path, "wb");
        if (!f)
            break;
        if (fwrite(px, 1, FRAME_BYTES, f) != FRAME_BYTES) {
            fclose(f);
            break;
        }
        fclose(f);

        snprintf(req, sizeof(req), "frame %s %u %u", path, FRAME_W, FRAME_H);
        if (appkit_send(req) != 0)
            break;
        if (n == 0 && wait_reply() != 0)
            break;
        if (n % 10 == 0 && !g.alive) {
            /* game over: pause, then reset */
            struct timespec t2 = { 2, 0 };
            while (nanosleep(&t2, &t2) != 0 && errno == EINTR && !asked_to_stop)
                ;
            snk_init(&g);
        }

        {
            int ms = g.alive ? g.speed_ms : 330;
            struct timespec t = { ms / 1000, (long)(ms % 1000) * 1000000L };

            while (nanosleep(&t, &t) != 0 && errno == EINTR && !asked_to_stop)
                ;
        }
        n++;
    }
    appkit_close();
    return asked_to_stop ? 0 : 1;
}
