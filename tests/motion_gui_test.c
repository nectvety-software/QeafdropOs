/* SPDX-License-Identifier: MIT */
/* tests/motion_gui_test.c - the wallpaper zoom/blur/dim transition, measured.
 *
 * This harness owns a real 1080x2340 RGB565 framebuffer and drives the same
 * compositor the phone runs, one synthetic 16 ms frame at a time. Everything it
 * asserts is a property of the pixels on that panel: how many distinct colours
 * are there, how much edge energy a scanline carries, whether two consecutive
 * frames differ. That is deliberate - a counter inside the compositor could
 * read "animated" while the glass stayed frozen, and three numbers that only
 * exist inside this file would pass even if the blitter drew nothing.
 *
 * Build and run (from repo root):  make motion-check
 * PNGs land next to the dumps; the contact sheet is meant to be looked at.
 */

#include "gui/compositor.h"
#include "gui/gfx/anim.h"
#include "gui/gfx/gfx.h"
#include "gui/gfx/theme.h"
#include "gui/gfx/wallpaper.h"
#include "gui/window_manager.h"
#include "apps/bootui/bootui.h"
#include "apps/launcher/home.h"
#include "apps/lock/lock_screen.h"
#include "core/sysio.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define W 1080
#define H 2340
#define STRIDE (W * 2)
#define FRAME_MS 16

static uint16_t fb[H][W];
static unsigned char seen[65536];

static int g_checks = 0;
static int g_fail = 0;
static uint64_t g_t;

static void check(int cond, const char *label) {
    g_checks++;
    if (cond) {
        printf("ok    %s\n", label);
    } else {
        g_fail++;
        printf("FAIL  %s\n", label);
    }
}

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

/* ---- the two screens this gate only needs as shapes ------------------------ */

/* A sheet: translucent plate plus its own opaque ink. This is the contract the
 * Wi-Fi screen keeps (apps/settings/wifiui.c), without dragging the radio seam
 * in here. */
static void sheet_draw(struct wm_window *w) {
    (void)w;
    bui_begin_overlay();
    bui_round(20, 60, 200, 120, 20, BUI_SURFACE, BUI_BORDER);
    bui_text("SHEET", 40, 200, 3, BUI_CYAN);
}

/* An opaque boot-mode screen: it repaints the panel, so the backdrop retires. */
static void opaque_draw(struct wm_window *w) {
    (void)w;
    bui_begin();
    bui_text("BOOT", 40, 400, 3, BUI_TEXT);
}

static struct wm_window g_sheet_win;
static struct wm_window g_opaque_win;

static void register_extra_windows(void) {
    g_sheet_win.name = "sheet";
    g_sheet_win.bounds = (struct umos_rect){0, 0, (uint16_t)W, (uint16_t)H};
    g_sheet_win.draw = sheet_draw;
    wm_register_window(UMOS_APP_SETTINGS, &g_sheet_win);

    g_opaque_win.name = "opaque";
    g_opaque_win.bounds = g_sheet_win.bounds;
    g_opaque_win.opaque = 1;
    g_opaque_win.draw = opaque_draw;
    wm_register_window(UMOS_APP_RECOVERY, &g_opaque_win);
}

/* ---- measurements over the real panel -------------------------------------- */

static uint16_t at_px(int x, int y) { return fb[y][x]; }

/* luma in 565 bit-space: monotone in each channel, which is all a comparison
 * between two frames of the same scene needs. */
static int luma(uint16_t v) {
    return (int)((v >> 11)) * 77 + (int)((v >> 5) & 63) * 151 + (int)(v & 31) * 28;
}

static uint64_t hash_fb(void) {
    uint64_t h = 1469598103934665603ULL;
    const uint16_t *p = &fb[0][0];
    for (size_t i = 0; i < (size_t)W * H; i++) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

/* Total horizontal edge energy. Blur is exactly the loss of this quantity, so
 * it is the number that says "the glass really got softer" - sampled on a grid
 * so it costs a few hundred thousand reads, not 2.5 million. */
static uint64_t edge_energy(void) {
    uint64_t e = 0;
    for (int y = 0; y < H; y += 7) {
        int prev = luma(at_px(0, y));
        for (int x = 3; x < W; x += 3) {
            int now = luma(at_px(x, y));
            int d = now - prev;
            e += (unsigned)(d < 0 ? -d : d);
            prev = now;
        }
    }
    return e;
}

static int distinct_colors(void) {
    int n = 0;
    memset(seen, 0, sizeof(seen));
    for (int y = 0; y < H; y += 3) {
        for (int x = 0; x < W; x += 3) {
            uint16_t v = at_px(x, y);
            if (!seen[v]) { seen[v] = 1; n++; }
        }
    }
    return n;
}

/* Same count, one box: this is how the gate asks whether the backdrop still
 * shows through the translucent part of a sheet, in a spot where only the
 * wash was drawn (no card border, no text). */
static int distinct_in(int x0, int y0, int x1, int y1) {
    int n = 0;
    memset(seen, 0, sizeof(seen));
    for (int y = y0; y < y1; y += 2) {
        for (int x = x0; x < x1; x += 2) {
            uint16_t v = at_px(x, y);
            if (!seen[v]) { seen[v] = 1; n++; }
        }
    }
    return n;
}

/* A patch of the full-screen wash - the translucent part of a sheet, as
 * opposed to the card drawn on top of it, which is an opaque fill by design.
 * This box is right of sheet_draw()'s card and across the aurora, the part of
 * the scene that carries light; over the dark sky even a translucent wash
 * rounds to one colour, and a test placed there would prove nothing. */
#define WASH_X0 700
#define WASH_Y0 200
#define WASH_X1 1040
#define WASH_Y1 520

static uint64_t mean_luma(void) {
    uint64_t s = 0, n = 0;
    for (int y = 0; y < H; y += 7) {
        for (int x = 0; x < W; x += 7) { s += (uint64_t)luma(at_px(x, y)); n++; }
    }
    return n ? s / n : 0;
}

static int dump(const char *path) {
    FILE *f = fopen(path, "wb");
    size_t n;
    if (!f) return 0;
    n = fwrite(&fb[0][0], 1, (size_t)W * H * 2, f);
    fclose(f);
    return n == (size_t)W * H * 2;
}

/* ---- driving the loop ------------------------------------------------------ */

static void frame(void) {
    g_t += FRAME_MS;
    compositor_frame(g_t);
}

/* Runs frames until the transition stops or the budget is gone. Returns how
 * many frames it took; the budget is what makes a runaway tween visible. */
static int run_until_settled(int budget) {
    int i;
    for (i = 1; i <= budget; i++) {
        frame();
        if (!compositor_needs_frame()) return i;
    }
    return budget + 1;
}

/* ---- the measurements the assertions compare ------------------------------- */

struct snap {
    uint64_t edges;
    uint64_t mean;
    int colors;
    int zoom, blur, scrim;
};

static void snap_of(struct snap *s) {
    s->edges = edge_energy();
    s->mean = mean_luma();
    s->colors = distinct_colors();
    s->zoom = compositor_zoom();
    s->blur = compositor_blur();
    s->scrim = compositor_scrim();
}

int main(void) {
    struct snap home, lock, sheet;
    uint64_t prev_hash = 0;
    int frames = 0, frames_new = 0, i, monotonic = 1;
    int first_blur_step = -1, last_blur_step = 0;
    int live_after_opaque, colors_opaque, wash_live, wash_flat, bg_wash_kept = 1;
    uint64_t ms_a, ms_b, cost_ramp, cost_settled, cost_frame;

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("== motion: wallpaper zoom/blur/dim, %dx%d, %d ms frames ==\n",
           W, H, FRAME_MS);
    gfx_init(&fb[0][0], W, H, STRIDE);
    theme_init();
    wm_init();
    compositor_init();
    home_init();
    lock_init();
    register_extra_windows();

    /* ---- the scene exists and covers the panel ---------------------------- */
    wallpaper_build();
    check(wallpaper_ready() == 1, "the backdrop builds once and stays built");
    check(wallpaper_composite(COMP_ZOOM_HOME, COMP_BLUR_HOME, COMP_SCRIM_HOME) ==
              (uint32_t)W * H,
          "a sharp composite writes every panel pixel, not a clipped band");
    check(wallpaper_pixels_last_composited() == (uint32_t)W * H,
          "the pixel count the compositor reports is the one it wrote");

    /* ---- HOME: sharp, unzoomed, undimmed ---------------------------------- */
    wm_open(UMOS_APP_HOME);
    frame();
    run_until_settled(4);
    snap_of(&home);
    check(home.zoom == COMP_ZOOM_HOME, "home sits at zoom 1.0");
    check(home.blur == COMP_BLUR_HOME, "home is unblurred");
    check(home.scrim == COMP_SCRIM_HOME, "home is undimmed");
    check(home.colors > 40, "the wallpaper puts many colours on home, not a fill");
    check(dump("out/motion_home.raw"), "wrote home RGB565 dump");

    /* ---- HOME -> LOCK: a ramp, not a cut --------------------------------- *
     * A tween stamps its start time on the first frame it is stepped, so frame
     * 0 of a transition is by construction still the previous image. The
     * frame-to-frame test begins at frame 1 for that reason: asking frame 0 to
     * differ from home would demand motion before the clock had advanced. */
    wm_open(UMOS_APP_LOCK);
    {
        int last_zoom = compositor_zoom();
        int last_blur = compositor_blur();
        int last_scrim = compositor_scrim();
        prev_hash = hash_fb();
        for (i = 0; i < 60; i++) {
            int dz, db, ds;
            frame();
            frames++;
            dz = compositor_zoom() - last_zoom;
            db = compositor_blur() - last_blur;
            ds = compositor_scrim() - last_scrim;
            if (dz < 0 || db < 0 || ds < 0) monotonic = 0;
            if (i > 0 && hash_fb() != prev_hash) frames_new++;
            prev_hash = hash_fb();
            if (db > 0) {
                if (first_blur_step < 0) first_blur_step = db;
                last_blur_step = db;
            }
            last_zoom = compositor_zoom();
            last_blur = compositor_blur();
            last_scrim = compositor_scrim();
            if (!compositor_needs_frame()) break;
        }
    }
    check(monotonic, "zoom, blur and dim only ever move toward the target");
    /* The tail of an ease-out is finer than one 565 step, so the honest claim
     * is a count of different images, not "all of them". Ten is the same floor
     * the frame budget uses; the byte-identical idle pair below is what proves
     * this comparison can see a difference at all. */
    check(frames_new >= 10, "the ramp puts at least ten different images on the panel");
    check(frames <= 45, "the transition settles inside 45 frames (750 ms)");
    check(frames >= 10, "it is a ramp: ten frames is the floor, not a jump");
    check(first_blur_step > last_blur_step,
          "the ease-out decelerates: the first blur step is bigger than the last");
    printf("      ramp: %d frames, %d of them new pictures, blur step %d -> %d\n",
           frames, frames_new, first_blur_step, last_blur_step);
    check(compositor_zoom() == COMP_ZOOM_COVER, "lock ends at the cover zoom");
    check(compositor_blur() == COMP_BLUR_COVER, "lock ends fully blurred");
    check(compositor_scrim() == COMP_SCRIM_LOCK, "lock ends dimmed");

    snap_of(&lock);
    check(dump("out/motion_lock.raw"), "wrote lock RGB565 dump");

    /* ---- the pixels got softer and darker, not just the counters ---------- */
    printf("      edges home=%llu lock=%llu  colors home=%d lock=%d  mean %llu->%llu\n",
           (unsigned long long)home.edges, (unsigned long long)lock.edges,
           home.colors, lock.colors,
           (unsigned long long)home.mean, (unsigned long long)lock.mean);
    check(lock.edges < home.edges, "the blurred frame carries less edge energy");
    check(lock.edges * 2 < home.edges,
          "and it is at least halved - a visible softening, not a rounding");
    check(lock.mean <= home.mean, "dimming the backdrop lowers its mean luma");
    check(lock.colors < home.colors, "blur merges colours: fewer distinct ones");

    /* ---- scrim alone: same zoom, same blur, only the dim changes ---------- */
    {
        uint64_t bright, dim;
        wallpaper_composite(COMP_ZOOM_COVER, COMP_BLUR_COVER, 0);
        bright = mean_luma();
        wallpaper_composite(COMP_ZOOM_COVER, COMP_BLUR_COVER, COMP_SCRIM_LOCK);
        dim = mean_luma();
        printf("      scrim alone: mean %llu -> %llu\n",
               (unsigned long long)bright, (unsigned long long)dim);
        check(dim < bright, "the scrim is what darkens it, measured on its own");
    }
    /* ---- blur alone: same zoom, only the softness changes ----------------- */
    {
        uint64_t sharp, soft;
        wallpaper_composite(COMP_ZOOM_COVER, COMP_BLUR_HOME, 0);
        sharp = edge_energy();
        wallpaper_composite(COMP_ZOOM_COVER, COMP_BLUR_COVER, 0);
        soft = edge_energy();
        printf("      blur alone: edges %llu -> %llu\n",
               (unsigned long long)sharp, (unsigned long long)soft);
        check(soft * 2 < sharp, "the blur ramp is a real loss of detail");
    }

    /* ---- idle: nothing moves, so nothing repaints ------------------------- */
    frame();
    prev_hash = hash_fb();
    frame();
    check(hash_fb() == prev_hash,
          "settled frames are byte-identical - the wallpaper is not rebuilt");

    /* ---- an opaque screen retires the backdrop ---------------------------- */
    wm_open(UMOS_APP_RECOVERY);
    frame();
    live_after_opaque = compositor_backdrop_live();
    colors_opaque = distinct_colors();
    printf("      opaque: %d colours (home has %d)\n", colors_opaque, home.colors);
    check(live_after_opaque == 0, "an opaque window tells the compositor to stop");
    check(wallpaper_last_zoom() == COMP_ZOOM_COVER,
          "and the backdrop really is frozen: no newer composite ran");
    check(colors_opaque < 10,
          "no wallpaper ink under it: poster navy plus its own text only");
    check(dump("out/motion_opaque.raw"), "wrote opaque RGB565 dump");

    /* ---- a sheet keeps the wallpaper under it ----------------------------- */
    wm_open(UMOS_APP_SETTINGS);
    frame();
    run_until_settled(45);
    snap_of(&sheet);
    wash_live = distinct_in(WASH_X0, WASH_Y0, WASH_X1, WASH_Y1);
    printf("      sheet: %d colours on the panel, %d in the wash patch\n",
           sheet.colors, wash_live);
    check(compositor_backdrop_live() == 1, "the sheet asks for the backdrop");
    check(sheet.scrim == COMP_SCRIM_COVER, "a sheet is not dimmed like lock");
    check(wash_live > 1,
          "wallpaper ink shows through the wash: that patch is not one colour");
    check(sheet.edges < home.edges, "the sheet frame is still the blurred backdrop");
    check(dump("out/motion_sheet.raw"), "wrote sheet RGB565 dump");

    /* ---- negative controls for the wash ---------------------------------- *
     * Over a flat poster background the very same patch must be one colour, and
     * that colour must be BUI_BG: blending the sheet colour over itself is the
     * identity. That is what keeps wifi-check and power-check measuring the
     * sheet the screen really draws instead of an empty framebuffer. */
    {
        struct umos_rect full = {0, 0, (uint16_t)W, (uint16_t)H};
        gfx_fill_rect(&full, BUI_BG);
        bui_begin_overlay();
        bui_round(20, 60, 200, 120, 20, BUI_SURFACE, BUI_BORDER);
        wash_flat = distinct_in(WASH_X0, WASH_Y0, WASH_X1, WASH_Y1);
        for (int y = WASH_Y0; y < WASH_Y1; y += 3)
            for (int x = WASH_X0; x < WASH_X1; x += 3)
                if (at_px(x, y) != rgb565(BUI_BG.r, BUI_BG.g, BUI_BG.b))
                    bg_wash_kept = 0;
        printf("      the same patch over a flat backdrop: %d colour(s)\n", wash_flat);
        check(wash_flat == 1,
              "the same patch over a flat backdrop is one colour - the count observes");
        check(bg_wash_kept,
              "the wash over BUI_BG returns BUI_BG exactly - blending is exact");
    }

    /* ---- the boot screen arrives settled ---------------------------------- *
     * init/main.c opens the lock screen as the very first window and then
     * calls compositor_settle(). Without that call the panel would ramp out of
     * a sharp home view nobody was ever shown, which reads as a glitch rather
     * than as motion. */
    compositor_init();
    wm_open(UMOS_APP_LOCK);
    compositor_settle();
    check(compositor_zoom() == COMP_ZOOM_COVER &&
          compositor_blur() == COMP_BLUR_COVER &&
          compositor_scrim() == COMP_SCRIM_LOCK,
          "a settled boot is already at the lock endpoints");
    check(compositor_needs_frame() == 0, "and it asks the loop for no ramp");
    frame();
    check(compositor_zoom() == COMP_ZOOM_COVER, "one frame later it has not moved");

    /* ---- the tween itself, at its endpoints ------------------------------- */
    check(anim_ease_out_cubic_q(0) == 0, "the easing starts at zero");
    check(anim_ease_out_cubic_q(ANIM_Q) == ANIM_Q, "the easing ends at one");
    check(anim_ease_out_cubic_q(128) > 128, "and it is ease-out, not linear");

    /* ---- host cost ------------------------------------------------------- *
     * Three numbers, because they answer different questions: what the blitter
     * costs mid-ramp (two mip levels sampled and mixed, its worst frame), what
     * it costs once settled (one level, the state the phone sits in between
     * gestures), and what a whole compositor frame costs with the sheet's wash
     * blended over it. The device has the same arithmetic and no faster memory,
     * so this is the figure that decides whether the ramp reads as motion. */
    ms_a = sysio_monotonic_ms();
    for (i = 0; i < 10; i++) wallpaper_composite(279, 128, 32);
    ms_b = sysio_monotonic_ms();
    cost_ramp = ms_b - ms_a;
    printf("host cost: backdrop mid-ramp   %4llu ms for 10 = %llu ms/frame\n",
           (unsigned long long)cost_ramp, (unsigned long long)(cost_ramp / 10));

    ms_a = sysio_monotonic_ms();
    for (i = 0; i < 10; i++)
        wallpaper_composite(COMP_ZOOM_COVER, COMP_BLUR_COVER, COMP_SCRIM_LOCK);
    ms_b = sysio_monotonic_ms();
    cost_settled = ms_b - ms_a;
    printf("host cost: backdrop settled    %4llu ms for 10 = %llu ms/frame\n",
           (unsigned long long)cost_settled, (unsigned long long)(cost_settled / 10));

    ms_a = sysio_monotonic_ms();
    for (i = 0; i < 10; i++) compositor_frame(g_t + (uint64_t)i);
    ms_b = sysio_monotonic_ms();
    cost_frame = ms_b - ms_a;
    printf("host cost: full frame + wash   %4llu ms for 10 = %llu ms/frame\n",
           (unsigned long long)cost_frame, (unsigned long long)(cost_frame / 10));

    check(ms_b >= ms_a, "the host clock answered the cost measurement");
    check(cost_frame < 1000,
          "ten real compositor frames finish inside a second here - the loop runs, it does not hang");
    check(cost_ramp < 1000, "and the worst frame of the ramp costs the same order");

    printf("\n%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
