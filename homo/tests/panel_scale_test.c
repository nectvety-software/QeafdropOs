/* SPDX-License-Identifier: MIT */
/* tests/panel_scale_test.c - host proof for the violet panel transform.
 *
 * The phone is 1080x2340 portrait and the desktop is 800x480 landscape, so
 * every frame is scaled by 2.25, letterboxed to 1800x1080 and rotated 90
 * degrees clockwise. Those four operations share one integer table in
 * platform/violet/panel_scale.c; this test renders with it and then reads the
 * pixels back, so a wrong sign in the rotation or an off-by-one in the
 * letterbox fails here instead of on the glass.
 *
 * No LVGL, no DRM, no phone: panel_scale.c is a pure function of its inputs.
 */
#define _POSIX_C_SOURCE 200809L
#include "platform/violet/panel_scale.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UI_W 800
#define UI_H 480
#define PANEL_W 1080
#define PANEL_H 2340
/* The portrait shell canvas: 540x1170: exactly half the panel in each axis. */
#define PORTRAIT_W 540
#define PORTRAIT_H 1170

static uint16_t ui[UI_W * UI_H];
static uint32_t full[PANEL_W * PANEL_H];
static uint32_t tiled[PANEL_W * PANEL_H];
static uint32_t scratch[PANEL_W * PANEL_H];

static void fail(const char * what, int a, int b, int c, int d)
{
    fprintf(stderr, "FAIL: %s (%d,%d) vs (%d,%d)\n", what, a, b, c, d);
    exit(1);
}

static void require(int condition, const char * what)
{
    if(!condition) {
        fprintf(stderr, "FAIL: %s\n", what);
        exit(1);
    }
}

static uint16_t color_of(int x, int y)
{
    /* Distinct per pixel in the low 11 bits so a shifted sample is visible. */
    return (uint16_t)(((x * 7 + y * 13) & 0x1F) << 11 |
                      ((x * 3 + y * 5) & 0x3F) << 5 |
                      ((x + y * 11) & 0x1F));
}

static uint32_t expand(uint16_t v)
{
    unsigned r = (v >> 11) & 0x1F, g = (v >> 5) & 0x3F, b = v & 0x1F;
    r = (r << 3) | (r >> 2);
    g = (g << 2) | (g >> 4);
    b = (b << 3) | (b >> 2);
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

/* Forward map used by the test only: UI pixel -> physical pixel. */
static void forward(const vio_panel_t *p, int ux, int uy, int *px, int *py)
{
    int lx = p->offset_x + (int)(((int64_t)ux * p->scale_q16) >> 16);
    int ly = p->offset_y + (int)(((int64_t)uy * p->scale_q16) >> 16);
    switch(p->rotation) {
    case 90:  *px = p->lh - 1 - ly; *py = lx; break;
    case 180: *px = p->lw - 1 - lx; *py = p->lh - 1 - ly; break;
    case 270: *px = ly; *py = p->lw - 1 - lx; break;
    default:  *px = lx; *py = ly; break;
    }
}

static int in_content(const vio_panel_t *p, int x, int y)
{
    int lx, ly;
    switch(p->rotation) {
    case 90:  lx = y; ly = p->lh - 1 - x; break;
    case 180: lx = p->lw - 1 - x; ly = p->lh - 1 - y; break;
    case 270: lx = p->lw - 1 - y; ly = x; break;
    default:  lx = x; ly = y; break;
    }
    return lx >= p->offset_x && lx < p->offset_x + p->out_w &&
           ly >= p->offset_y && ly < p->offset_y + p->out_h;
}

/* Renders the whole panel in tiles and requires it to equal one full render.
 * That is the real contract with LVGL: a partial flush must produce exactly
 * the same pixels as a full redraw. Letterbox pixels are deliberately not
 * touched by a partial flush (they are already black), so only the content
 * area is compared and the bars must stay untouched. */
static void check_tiles(const vio_panel_t *p, int rotation)
{
    int x, y;

    memset(full, 0, sizeof(full));
    vio_panel_render(p, ui, full, vio_panel_full_rect(p));

    memset(tiled, 0xEF, sizeof(tiled));
    for(y = 0; y < UI_H; y += 53) {
        for(x = 0; x < UI_W; x += 97) {
            vio_rect_t r;
            r.x1 = x;
            r.y1 = y;
            r.x2 = x + 96 > UI_W - 1 ? UI_W - 1 : x + 96;
            r.y2 = y + 52 > UI_H - 1 ? UI_H - 1 : y + 52;
            vio_panel_render(p, ui, tiled, vio_panel_map_ui_rect(p, r));
        }
    }
    for(y = 0; y < PANEL_H; y++) {
        for(x = 0; x < PANEL_W; x++) {
            size_t i = (size_t)y * PANEL_W + x;
            if(in_content(p, x, y)) {
                if(full[i] != tiled[i])
                    fail("tiled flush differs from full render", x, y,
                         (int)full[i], (int)tiled[i]);
            }
            else if(tiled[i] != 0xEFEFEFEFu) {
                fail("a partial flush painted a letterbox pixel", x, y,
                     (int)tiled[i], rotation);
            }
        }
    }
    printf("PASS rotation %3d: 800x480 -> %dx%d inside %dx%d, %d tiles match "
           "the full frame\n", rotation, p->out_w, p->out_h, p->lw, p->lh,
           ((UI_W + 96) / 97) * ((UI_H + 52) / 53));
}

static void check_letterbox(const vio_panel_t *p, int rotation)
{
    int x, y;
    memset(scratch, 0, sizeof(scratch));
    vio_panel_render(p, ui, scratch, vio_panel_full_rect(p));

    /* Nothing outside the scaled rectangle may receive UI colour, and the
     * inside corners must. */
    for(y = 0; y < p->panel_h; y++) {
        for(x = 0; x < p->panel_w; x++) {
            if(!in_content(p, x, y) &&
               scratch[(size_t)y * p->panel_w + x] != 0xFF000000u)
                fail("letterbox is not black", x, y, rotation, 0);
        }
    }
    printf("PASS rotation %3d: every letterbox pixel is black\n", rotation);
}

static void check_touch(const vio_panel_t *p, int rotation)
{
    int ux, uy, worst = 0;

    for(uy = 0; uy < UI_H; uy += 37) {
        for(ux = 0; ux < UI_W; ux += 41) {
            int px, py, got_x = -1, got_y = -1;
            forward(p, ux, uy, &px, &py);
            if(vio_panel_touch_map(p, px, py, p->panel_w, p->panel_h,
                                   &got_x, &got_y) != 0)
                fail("touch in the content area was rejected", px, py, ux, uy);
            int dx = got_x - ux, dy = got_y - uy;
            if(dx < 0) dx = -dx;
            if(dy < 0) dy = -dy;
            if(dx > worst) worst = dx;
            if(dy > worst) worst = dy;
            if(dx > 1 || dy > 1)
                fail("touch round-trip is off by more than one pixel",
                     ux, uy, got_x, got_y);
        }
    }

    /* A tap in the letterbox must not become a tap on the desktop. */
    {
        int probes[4][2] = {{0, 0}, {PANEL_W - 1, 0}, {0, PANEL_H - 1},
                            {PANEL_W - 1, PANEL_H - 1}};
        for(int i = 0; i < 4; i++) {
            int got_x = 0, got_y = 0;
            if(vio_panel_touch_map(p, probes[i][0], probes[i][1],
                                   p->panel_w, p->panel_h,
                                   &got_x, &got_y) == 0)
                fail("letterbox tap was accepted", probes[i][0], probes[i][1],
                     got_x, got_y);
        }
    }

    printf("PASS rotation %3d: touch inverse round-trip within %d px, "
           "letterbox taps rejected\n", rotation, worst);
}

static void check_geometry(void)
{
    vio_panel_t p;
    vio_panel_init(&p, UI_W, UI_H, PANEL_W, PANEL_H, 90);

    require(p.rotation == 90, "rotation 90 was not accepted");
    require(p.lw == PANEL_H && p.lh == PANEL_W, "90 degrees must swap the axes");
    require(p.stride == PANEL_W, "a fresh panel must render with a packed stride");
    require(p.scale_q16 == 147456, "800x480 on 2340x1080 must scale by 2.25");
    require(p.out_w == 1800 && p.out_h == 1080, "scaled size must be 1800x1080");
    require(p.offset_x == 270 && p.offset_y == 0, "letterbox must be 270 px per side");

    /* The 90-degree clockwise map: UI top-left lands at panel top-right. */
    require(p.map_lx[270] == 0 && p.map_ly[0] == 0, "UI (0,0) must map to lx=0,ly=0");
    require(p.map_lx[2069] == 799 && p.map_ly[1079] == 479,
            "UI (799,479) must map to the last content pixel");
    require(p.map_lx[269] == -1 && p.map_lx[2070] == -1,
            "the letterbox columns must be unmapped");
    printf("PASS geometry: 2.25x, 1800x1080, 270 px letterbox, 90 CW\n");

    vio_panel_init(&p, UI_W, UI_H, PANEL_W, PANEL_H, 0);
    require(p.lw == PANEL_W && p.lh == PANEL_H, "0 degrees must keep the panel axes");
    require(p.out_w == 1080 && p.out_h == 648, "portrait fit must be 1080x648");
    require(p.offset_y == 846, "portrait letterbox must be 846 px top and bottom");
    printf("PASS geometry: portrait fit 1080x648 centred with 846 px bars\n");

    vio_panel_set_rotation(&p, 450);
    require(p.rotation == 90, "450 degrees must normalise to 90");
    vio_panel_set_rotation(&p, -90);
    require(p.rotation == 270, "-90 degrees must normalise to 270");
    vio_panel_set_rotation(&p, 45);
    require(p.rotation == 0, "an unsupported angle must fall back to 0");
    printf("PASS geometry: angles normalise to 0/90/180/270\n");
}

static void check_stride(void)
{
    static uint32_t strided[1088 * PANEL_H];
    vio_panel_t p;
    int x, y;

    vio_panel_init(&p, UI_W, UI_H, PANEL_W, PANEL_H, 90);
    vio_panel_set_stride(&p, 1088);
    require(p.stride == 1088, "the DRM pitch stride was not accepted");

    memset(strided, 0x7F, sizeof(strided));
    vio_panel_render(&p, ui, strided, vio_panel_full_rect(&p));
    vio_panel_set_stride(&p, PANEL_W);
    memset(full, 0, sizeof(full));
    vio_panel_render(&p, ui, full, vio_panel_full_rect(&p));

    for(y = 0; y < PANEL_H; y++) {
        for(x = 0; x < PANEL_W; x++)
            if(strided[(size_t)y * 1088 + x] != full[(size_t)y * PANEL_W + x])
                fail("strided render differs from the packed render", x, y,
                     (int)strided[(size_t)y * 1088 + x],
                     (int)full[(size_t)y * PANEL_W + x]);
        for(x = PANEL_W; x < 1088; x++)
            if(strided[(size_t)y * 1088 + x] != 0x7F7F7F7Fu)
                fail("strided render painted row padding", x, y,
                     (int)strided[(size_t)y * 1088 + x], 0);
    }
    printf("PASS stride: a 1088-pixel DRM pitch renders the same frame and "
           "leaves the 8-pixel pad untouched\n");
}

static void check_portrait_shell(void)
{
    static uint16_t ui_portrait[PORTRAIT_W * PORTRAIT_H];
    static uint32_t shot[PANEL_W * PANEL_H];
    vio_panel_t p;
    int x, y;

    for(y = 0; y < PORTRAIT_H; y++)
        for(x = 0; x < PORTRAIT_W; x++)
            ui_portrait[(size_t)y * PORTRAIT_W + x] = color_of(x, y);

    vio_panel_init(&p, PORTRAIT_W, PORTRAIT_H, PANEL_W, PANEL_H, 0);
    require(p.lw == PANEL_W && p.lh == PANEL_H,
            "portrait must keep the panel axes");
    require(p.scale_q16 == 131072, "540x1170 must scale by exactly 2.0");
    require(p.out_w == PANEL_W && p.out_h == PANEL_H,
            "the portrait shell must fill the panel");
    require(p.offset_x == 0 && p.offset_y == 0,
            "the portrait shell must not be letterboxed");

    memset(shot, 0, sizeof(shot));
    vio_panel_render(&p, ui_portrait, shot, vio_panel_full_rect(&p));
    for(y = 0; y < PANEL_H; y += 97) {
        for(x = 0; x < PANEL_W; x += 97) {
            uint32_t want = expand(ui_portrait[(size_t)(y / 2) * PORTRAIT_W + x / 2]);
            if(shot[(size_t)y * PANEL_W + x] != want)
                fail("portrait sample is not the UI pixel above it", x, y,
                     (int)shot[(size_t)y * PANEL_W + x], (int)want);
        }
    }
    require(shot[(size_t)(PANEL_H - 1) * PANEL_W + (PANEL_W - 1)] ==
            expand(ui_portrait[(size_t)(PORTRAIT_H - 1) * PORTRAIT_W +
                               (PORTRAIT_W - 1)]),
            "the portrait shell's last UI pixel must reach the panel corner");
    printf("PASS portrait shell: 540x1170 fills 1080x2340 at 2.0x, no bars\n");

    /* Switching an existing panel between the two canvases must keep the DRM
     * stride and the rotation, and drop / restore the letterbox. */
    vio_panel_init(&p, UI_W, UI_H, PANEL_W, PANEL_H, 0);
    vio_panel_set_stride(&p, 1088);
    require(p.out_h == 648, "the landscape canvas is letterboxed in portrait");
    vio_panel_set_ui_size(&p, PORTRAIT_W, PORTRAIT_H);
    require(p.out_w == PANEL_W && p.out_h == PANEL_H && p.offset_y == 0,
            "switching the canvas to portrait must fill the panel");
    require(p.stride == 1088, "a canvas change must keep the DRM stride");
    require(p.rotation == 0, "a canvas change must keep the rotation");

    vio_panel_init(&p, PORTRAIT_W, PORTRAIT_H, PANEL_W, PANEL_H, 0);
    vio_panel_set_ui_size(&p, UI_W, UI_H);
    require(p.out_w == PANEL_W && p.out_h == 648 && p.offset_y == 846,
            "the 800x480 app canvas must be letterboxed inside portrait");
    vio_panel_set_ui_size(&p, PORTRAIT_W, PORTRAIT_H);
    require(p.out_w == PANEL_W && p.out_h == PANEL_H && p.offset_y == 0,
            "returning to the shell must fill the panel again");
    printf("PASS canvas switch: 540x1170 <-> 800x480 keeps stride and rotation\n");
}

int main(void)
{
    static const int rotations[] = {0, 90, 180, 270};
    vio_panel_t panel;

    for(int y = 0; y < UI_H; y++)
        for(int x = 0; x < UI_W; x++)
            ui[(size_t)y * UI_W + x] = color_of(x, y);

    check_geometry();
    check_portrait_shell();

    for(size_t i = 0; i < sizeof(rotations) / sizeof(rotations[0]); i++) {
        vio_panel_init(&panel, UI_W, UI_H, PANEL_W, PANEL_H, rotations[i]);
        check_tiles(&panel, rotations[i]);
        check_letterbox(&panel, rotations[i]);
        check_touch(&panel, rotations[i]);
    }

    /* A rect that is entirely outside the UI must map to the empty rect. */
    vio_panel_init(&panel, UI_W, UI_H, PANEL_W, PANEL_H, 90);
    vio_rect_t empty = vio_panel_map_ui_rect(&panel, (vio_rect_t){-40, -40, -1, -1});
    require(empty.x1 > empty.x2 || empty.y1 > empty.y2,
            "an off-screen UI rect must map to an empty rect");
    printf("PASS: off-screen UI rect maps to the empty rect\n");

    /* Colour check: the pixel sampled at a known panel position is the pixel
     * the forward map names, not its neighbour. */
    memset(full, 0, sizeof(full));
    vio_panel_render(&panel, ui, full, vio_panel_full_rect(&panel));
    int px = PANEL_W - 1, py = 0;            /* panel top-right */
    require(full[(size_t)py * PANEL_W + px] == expand(ui[0]), "UI top-left must land at panel top-right");
    py = 1080;                                /* lx=1080 -> UI x=360, ly=0 */
    require(full[(size_t)py * PANEL_W + px] == expand(ui[360]),
            "panel column 1079, row 1080 must sample UI x=360,y=0");
    printf("PASS: sampled pixels follow the 90 CW rotation\n");

    check_stride();

    puts("panel_scale_test: all checks passed");
    return 0;
}
