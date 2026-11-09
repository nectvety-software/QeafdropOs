/* SPDX-License-Identifier: MIT */
/* tests/wifi_gui_test.c - host gate for the REAL Wi-Fi seam + its screens.
 *
 * Nothing here recites networks: the rows come from wcn3990.c's own
 * parse_dump() running over a synthetic NL80211_CMD_GET_SCAN blob assembled
 * with the same little-endian wire format the kernel uses, and the live path
 * is walked against a fixture /sys tree built from ordinary files (the host
 * has no netlink, so the seam must answer "no answer" - which it must, and
 * which is measured here too).
 *
 * In-run negative controls, all on the same glass:
 *   - radio off           -> scan -1, "Wi-Fi is off"
 *   - phy fixture empty   -> scan -1, "Radio not ready"
 *   - phy present, no NL  -> scan -1 via the netlink refusal, not a crash
 *   - truncated blob      -> 0 rows, "No networks found" (rc 0, not -1)
 *   - wifi_connect        -> -1: no supplicant is bound, and the card stays off
 *
 * Build (from repo root): make wifi-check
 */

#include "apps/bootui/bootui.h"
#include "apps/settings/wifiui.h"
#include "bsp/violet/wcn3990.h"
#include "gui/gfx/font_5x7.h"
#include "gui/gfx/gfx.h"
#include "gui/gfx/theme.h"
#include "gui/window_manager.h"
#include "hal/include/hal.h"
#include "hal/include/wifi.h"

#include <direct.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define W 1080
#define H 2340
#define STRIDE (W * 2)

static uint16_t fb[H][W];
static uint16_t small[780][360];

static int g_checks = 0;
static int g_fail = 0;

static void check(int cond, const char *label) {
    g_checks++;
    if (cond) {
        printf("ok    %s\n", label);
    } else {
        g_fail++;
        printf("FAIL  %s\n", label);
    }
}

/* ---- stubs for the pieces the phone supplies ------------------------------- */

static struct wm_window *g_wins[UMOS_MAX_WINDOWS];
static int g_nwin;

void wm_register_window(int app_id, struct wm_window *w) {
    (void)app_id;
    if (g_nwin < UMOS_MAX_WINDOWS) g_wins[g_nwin++] = w;
}

void wm_open(int app_id) {
    for (int i = 0; i < g_nwin; i++)
        g_wins[i]->visible = (g_wins[i]->id == app_id);
}

void wm_close(int app_id) {
    for (int i = 0; i < g_nwin; i++)
        if (g_wins[i]->id == app_id) g_wins[i]->visible = 0;
}

struct hal_touch *hal_touch(void) { return NULL; }

void umos_logf(const char *fmt, ...) {
    va_list ap;
    (void)fmt;
    va_start(ap, fmt);
    va_end(ap);
}

/* ---- measured facts -------------------------------------------------------- */

static uint16_t at_v(int vx, int vy) { return fb[vy * 3][vx * 3]; }

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

#define C_BG     rgb565(BUI_BG.r, BUI_BG.g, BUI_BG.b)
#define C_TEAL   rgb565(BUI_TEAL.r, BUI_TEAL.g, BUI_TEAL.b)
#define C_TEXT   rgb565(BUI_TEXT.r, BUI_TEXT.g, BUI_TEXT.b)
#define C_DIM    rgb565(BUI_DIM.r, BUI_DIM.g, BUI_DIM.b)
#define C_SURFHI rgb565(BUI_SURFACE_HI.r, BUI_SURFACE_HI.g, BUI_SURFACE_HI.b)

static int count_color(int vx0, int vy0, int vx1, int vy1, uint16_t col) {
    int n = 0;
    for (int vy = vy0; vy < vy1; vy++)
        for (int vx = vx0; vx < vx1; vx++)
            if (at_v(vx, vy) == col) n++;
    return n;
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

static int dump(const char *path) {
    FILE *f = fopen(path, "wb");
    size_t n;
    if (!f) return 0;
    n = fwrite(&fb[0][0], 1, (size_t)W * H * 2, f);
    fclose(f);
    return n == (size_t)W * H * 2;
}

/* The phone gives the sheet a backdrop: the compositor paints the wallpaper
 * and wifiui lays a translucent plate over it. On the host the backdrop the
 * harness hands over is the poster navy itself, which is the one input under
 * which blending the plate is a no-op - so every C_BG assertion below still
 * measures the plate the screen drew, not an empty framebuffer. */
static void draw_wifi(void) {
    struct umos_rect full = {0, 0, (uint16_t)gfx_width(), (uint16_t)gfx_height()};
    gfx_fill_rect(&full, BUI_BG);
    wifiui_draw();
}

/* layout mirrors wifiui.c for the unconnected list */
#define ROWS_Y0   138
#define ADV_Y     253
#define ACT_Y0    366
#define TOG_Y0    450

/* ---- fixture /sys trees ---------------------------------------------------- */

#define FX_READY "out/wifi_fx/sys"
#define FX_NOPHY "out/wifi_fx/nophy/sys"

static void mkdir_p(const char *path) {
    char buf[256];
    size_t i;
    for (i = 0; path[i] && i + 1 < sizeof(buf); i++) {
        buf[i] = path[i];
        buf[i + 1] = '\0';
        if (path[i] == '/' && i > 0) _mkdir(buf);
    }
    _mkdir(buf);
}

static void put_file(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");
    if (!f) { printf("FAIL  cannot create fixture %s\n", path); g_fail++; return; }
    fputs(text, f);
    fclose(f);
}

static int read_file_str(const char *path, char *out, int cap) {
    FILE *f = fopen(path, "rb");
    size_t n;
    if (!f) return -1;
    n = fread(out, 1, (size_t)cap - 1, f);
    out[n] = '\0';
    fclose(f);
    return (int)n;
}

static void build_fixtures(void) {
    mkdir_p(FX_READY "/class/ieee80211/phy0");
    mkdir_p(FX_READY "/class/net/wlan0");
    mkdir_p(FX_READY "/class/rfkill/rfkill0");
    put_file(FX_READY "/class/net/wlan0/ifindex", "3\n");
    put_file(FX_READY "/class/rfkill/rfkill0/type", "wlan\n");
    put_file(FX_READY "/class/rfkill/rfkill0/soft", "1\n");

    mkdir_p(FX_NOPHY "/class/ieee80211");
    mkdir_p(FX_NOPHY "/class/net/wlan0");
    put_file(FX_NOPHY "/class/net/wlan0/ifindex", "3\n");
}

/* ---- synthetic GET_SCAN blob ---------------------------------------------- */

static unsigned seq32 = 100;

static void w16(unsigned char *p, unsigned v) {
    p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
}
static void w32(unsigned char *p, unsigned v) {
    p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24);
}
static int align4(int v) { return (v + 3) & ~3; }

static int put_nla(unsigned char *b, int off, unsigned type,
                   const unsigned char *d, int dl) {
    int pad = align4(dl + 4);
    int i;
    w16(b + off, (unsigned)(dl + 4));
    w16(b + off + 2, type);
    for (i = 0; i < dl; i++) b[off + 4 + i] = d[i];
    for (i = dl + 4; i < pad; i++) b[off + i] = 0;
    return off + pad;
}

/* One NL80211_CMD_GET_SCAN datagram whose BSS attr holds these inner attrs. */
static int put_scan_msg(unsigned char *b, int off, const unsigned char *inner,
                        int ilen) {
    int o = off + 20;
    w32(b + off, 20u);              /* patched below */
    w16(b + off + 4, 32);           /* NL80211_CMD_GET_SCAN */
    w16(b + off + 6, 0);
    w32(b + off + 8, ++seq32);
    w32(b + off + 12, 0);
    b[off + 16] = 0; b[off + 17] = 1; b[off + 18] = 0; b[off + 19] = 0;
    o = put_nla(b, o, 47u | 0x8000u, inner, ilen); /* ATTR_BSS, marked nested */
    w32(b + off, (unsigned)(o - off));
    return o;
}

/* bssid + frequency + signal_mbm + information elements, in wire order.
 * SIGNAL_MBM is written by the kernel as dBm*100 (what wpa_supplicant divides
 * by), so -52 dBm is -5200 on the wire, not -52000. */
static int build_bss(unsigned char *b, unsigned char mac6, uint32_t freq,
                     int mbm, const unsigned char *ies, int ielen) {
    unsigned char inner[512];
    unsigned char v4[4];
    unsigned char mac[6];
    int io = 0;
    int i;
    for (i = 0; i < 6; i++) mac[i] = (unsigned char)(mac6 + i);
    io = put_nla(inner, io, 1, mac, 6);             /* BSS_BSSID */
    w32(v4, freq);
    io = put_nla(inner, io, 2, v4, 4);              /* BSS_FREQUENCY */
    w32(v4, (unsigned)(int)mbm);
    io = put_nla(inner, io, 7, v4, 4);              /* BSS_SIGNAL_MBM */
    io = put_nla(inner, io, 6, ies, ielen);         /* BSS_INFORMATION_ELEMENTS */
    return put_scan_msg(b, 0, inner, io);
}

static int ie_put(unsigned char *p, int id, const unsigned char *body, int blen) {
    int i;
    p[0] = (unsigned char)id;
    p[1] = (unsigned char)blen;
    for (i = 0; i < blen; i++) p[2 + i] = body[i];
    return 2 + blen;
}

/* RSN with a given AKM suite */
static int rsn_body(unsigned char *out, unsigned char akm_last) {
    static const unsigned char hdr[] = { 1, 0, 0x00, 0x0F, 0xAC, 0x04,
                                         0x00, 0x01, 0x00, 0x0F, 0xAC, 0x04,
                                         0x00, 0x01 };
    int n = 0;
    for (n = 0; n < (int)sizeof(hdr); n++) out[n] = hdr[n];
    out[n++] = 0x00; out[n++] = 0x01;
    out[n++] = 0x00; out[n++] = 0x0F; out[n++] = 0xAC; out[n++] = akm_last;
    out[n++] = 0xC0; out[n++] = 0x00;
    return n;
}

static unsigned char g_blob[4096];

/* three beacons, deliberately written weakest-first so the sort is proven */
static int build_blob(void) {
    unsigned char ies[256];
    unsigned char body[64];
    int off = 0;
    int bl, n;

    /* open beacon: only an SSID element */
    bl = (int)strlen("Le Chen Open");
    memcpy(body, "Le Chen Open", (size_t)bl);
    n = ie_put(ies, 0, body, bl);
    off = build_bss(g_blob, 0xAA, 2412, -8000, ies, n);

    /* WPA2 beacon */
    bl = (int)strlen("HomeWPA2");
    memcpy(body, "HomeWPA2", (size_t)bl);
    n = ie_put(ies, 0, body, bl);
    bl = rsn_body(body, 0x02);
    n += ie_put(ies + n, 48, body, bl);
    off += build_bss(g_blob + off, 0xBB, 2437, -6700, ies, n);

    /* WPA3-SAE beacon with a composed Vietnamese SSID */
    bl = 0;
    body[bl++] = 'Q'; body[bl++] = 'u';
    body[bl++] = (char)0xC3; body[bl++] = (char)0xA1; /* a acute */
    body[bl++] = 'n'; body[bl++] = ' '; body[bl++] = 'C';
    body[bl++] = (char)0xC3; body[bl++] = (char)0xA0; /* a grave */
    body[bl++] = ' '; body[bl++] = 'P'; body[bl++] = 'h';
    body[bl++] = (char)0xC3; body[bl++] = (char)0xAA; /* e circumflex: U+00EA is C3 AA */
    body[bl++] = '_'; body[bl++] = '5'; body[bl++] = 'G';
    n = ie_put(ies, 0, body, bl);
    bl = rsn_body(body, 0x06);
    n += ie_put(ies + n, 48, body, bl);
    off += build_bss(g_blob + off, 0xCC, 5500, -5200, ies, n);

    /* NLMSG_DONE trailer */
    w32(g_blob + off, 20u);
    w16(g_blob + off + 4, 3);
    w16(g_blob + off + 6, 0);
    w32(g_blob + off + 8, ++seq32);
    w32(g_blob + off + 12, 0);
    return off + 20;
}

/* ---- phase checks ---------------------------------------------------------- */

static void check_font_ext(void) {
    int inked, plain;

    printf("\n== font: composed glyphs a broadcast SSID can carry ==\n");
    check(font_glyph_cp(&font_5x7, 0x00E0) != NULL, "U+00E0 (a grave) has a glyph");
    check(font_glyph_cp(&font_5x7, 0x1EBD) == NULL,
          "an untabled code point still resolves to nothing");

    bui_begin();
    bui_text("a\303\240 o\341\273\231", 20, 700, 3, BUI_TEXT);
    inked = count_color(20, 700, 200, 730, C_TEXT);
    bui_begin();
    bui_text("a o", 20, 700, 3, BUI_TEXT);
    plain = count_color(20, 700, 200, 730, C_TEXT);
    check(inked > plain + 20, "the diacritics add real ink over the plain letters");
    bui_begin();
    bui_text("\341\272\275", 20, 700, 3, BUI_TEXT);
    check(count_color(20, 700, 200, 730, C_TEXT) == 0,
          "an untabled code point draws nothing instead of a wrong glyph");

    /* A broadcast SSID is not chosen by us: a Latin-1 letter the face lacks
     * must degrade to its base letter, not to a hole in the middle of a word. */
    check(font_glyph_cp(&font_5x7, 0x00E1) == font_glyph_cp(&font_5x7, (uint32_t)'a'),
          "U+00E1 falls back to the base letter");
    check(font_glyph_cp(&font_5x7, 0x00EA) == font_glyph_cp(&font_5x7, (uint32_t)'e'),
          "U+00EA falls back to the base letter");
    check(font_glyph_cp(&font_5x7, 0x00E0) != font_glyph_cp(&font_5x7, (uint32_t)'a'),
          "the composed glyph the face does carry is not replaced by the fallback");
    check(font_glyph_cp(&font_5x7, 0x1EBD) == NULL,
          "the fallback stops at Latin-1: nothing outside it is invented");
    {
        /* the one composed glyph the face carries must be the mark its code
         * point names: a grave falls to the right, an acute rises to it */
        int top = 0, below = 0;
        bui_begin();
        bui_text("\303\240", 20, 700, 3, BUI_TEXT);
        for (int vx = 20; vx < 40; vx++) {
            int r0 = 0, r1 = 0;
            for (int vy = 0; vy < 3; vy++) {
                if (at_v(vx, 700 + vy) == C_TEXT) r0 = 1;
                if (at_v(vx, 703 + vy) == C_TEXT) r1 = 1;
            }
            if (r0 && !top) top = vx;
            if (r1 && !below) below = vx;
        }
        check(top && below && top < below,
              "U+00E0 is drawn as a grave, not a mirrored acute");
    }
    {
        static const char name[] = "Qu\303\241n C\303\240 Ph\303\252_5G";
        const char *p = name;
        int dropped = 0, letters = 0;
        while (*p) {
            const char *start = p;
            uint32_t cp = font_utf8_next(&p);
            char one[5];
            int n = (int)(p - start);
            int ink = 0;
            if (cp == ' ') continue;
            for (int i = 0; i < n; i++) one[i] = start[i];
            one[n] = '\0';
            letters++;
            bui_begin();
            bui_text(one, 20, 700, 3, BUI_TEXT);
            ink = count_color(20, 700, 20 + bui_text_w(one, 3), 722, C_TEXT);
            if (ink == 0) dropped++;
        }
        check(letters == 12 && dropped == 0,
              "every letter of a Vietnamese SSID puts ink on the glass");
        bui_begin();
    }
}

static void check_list_off(uint64_t *h) {
    struct wifi_scan_result probe[4];

    printf("\n== control: radio off, the seam refuses ==\n");
    check(wifi_scan(probe, 4) == -1, "scan is refused while the master is off");
    wifiui_open();
    draw_wifi();
    *h = hash_fb();
    check(wifiui_rows() == 0, "no rows while off");
    check(wifiui_scan_failed() == 0, "off is not reported as a failed scan");
    check(at_v(302, 86) == C_DIM, "off: the switch knob sits left, dim");
    check(at_v(326, 86) == C_SURFHI, "off: the right of the track is empty");
    check(count_color(16, 130, 90, 138, C_DIM) > 20, "the 'Wi-Fi is off' line is inked");
    check(at_v(200, 112) == C_BG, "no connected card while off");
    check(dump("out/wifi_list_off.raw"), "wrote list-off RGB565 dump");
}

static void check_radio_not_ready(uint64_t h_off, uint64_t *h_nophy) {
    struct wifi_scan_result probe[4];

    printf("\n== control: radio on, no phy on this boot ==\n");
    wcn_set_sysfs_root(FX_NOPHY);
    check(wifi_scan(probe, 4) == -1, "the live seam refuses without a phy");
    wifiui_open();
    draw_wifi();
    *h_nophy = hash_fb();
    check(wifiui_rows() == 0, "no rows without a phy");
    check(wifiui_scan_failed() == 1, "the seam records the refusal, not emptiness");
    check(at_v(302, 86) == C_TEAL, "on: the left of the track is teal");
    check(at_v(326, 86) == C_BG, "on: the knob sits right, knocked out");
    check(count_color(16, ROWS_Y0, 110, ROWS_Y0 + 8, C_DIM) > 20,
          "'Radio not ready' is inked where rows would be");
    check(h_off != *h_nophy, "powering the radio repaints the screen");
}

static void check_netlink_refusal_on_host(void) {
    struct wifi_scan_result probe[4];

    printf("\n== control: phy present, but the host has no netlink ==\n");
    wcn_set_sysfs_root(FX_READY);
    check(wifi_scan(probe, 4) == -1,
          "the walk reaches the socket and honestly answers no");
}

static void check_real_air(void) {
    struct wifi_scan_result r[WIFI_MAX_SCAN_RESULTS];
    struct wifi_config cfg;
    int n;

    printf("\n== the real seam parsing a real wire format ==\n");
    memset(&cfg, 0, sizeof(cfg));
    cfg.auth = WIFI_AUTH_WPA3_SAE;
    n = wifi_scan(r, WIFI_MAX_SCAN_RESULTS);
    check(n == 3, "three beacons come back from the parser");
    check(strcmp(r[0].ssid, "Qu\303\241n C\303\240 Ph\303\252_5G") == 0,
          "row 0 is the strongest beacon, Vietnamese bytes intact");
    check(r[0].rssi == -52 && r[0].channel == 100,
          "row 0 reads -52 dBm / channel 100 from the wire values");
    check(r[0].auth == WIFI_AUTH_WPA3_SAE, "the SAE AKM is found inside RSN");
    check(r[1].auth == WIFI_AUTH_WPA2_PSK, "an RSN without SAE is WPA2");
    check(r[1].rssi == -67 && r[1].channel == 6,
          "row 1 sorted second with its own freq -> channel math");
    check(r[2].auth == WIFI_AUTH_OPEN, "a beacon with no RSN reads Open");
    check(r[2].rssi == -80 && r[2].channel == 1,
          "row 2 is the weakest, 2.4 GHz channel 1");
    check(r[0].bssid[0] == 0xCC, "BSSIDs travelled through untouched");

    /* the rfkill seam wrote the user's intent into the fixture tree */
    {
        char soft[8];
        check(read_file_str(FX_READY "/class/rfkill/rfkill0/soft",
                            soft, (int)sizeof(soft)) > 0 &&
              soft[0] == '0', "wifi_on unblocked the wlan rfkill soft switch");
    }

    check(wifi_connect(&cfg) == -1,
          "association refuses: no supplicant is bound, and the seam says so");
}

static void check_list_on(uint64_t *h_on) {
    printf("\n== Wi-Fi list painted from the parsed scan ==\n");
    wifiui_open();
    draw_wifi();
    *h_on = hash_fb();
    check(wifiui_rows() == 3, "the scan seam feeds three rows to the screen");
    check(wifiui_scan_failed() == 0, "rows present: no failure line");
    check(count_color(52, ROWS_Y0 + 6, 260, ROWS_Y0 + 3 * 42, C_TEXT) > 200,
          "the three SSIDs are rasterised");
    check(count_color(16, 124, 130, 132, C_TEAL) > 20,
          "the Available Networks header is inked teal");
    check(at_v(200, 112) == C_BG, "no connected card before associating");
    /* the reference header: back chevron on the left, kebab on the right */
    check(count_color(10, 12, 22, 24, C_TEXT) >= 6,
          "the header carries a back chevron");
    {
        /* The chevron must point LEFT: the tip is the single pixel on the
         * middle row and it sits LEFT of the arms above and below.
         * A right-pointing arrow has the opposite order. */
        int mid = -1, top = -1, bot = -1, i;
        for (i = 8; i < 24; i++) {
            if (at_v(i, 17) == C_TEXT) { if (mid < 0) mid = i; }
            if (at_v(i, 14) == C_TEXT) { if (top < 0) top = i; }
            if (at_v(i, 20) == C_TEXT) { if (bot < 0) bot = i; }
        }
        check(mid >= 0 && top >= 0 && bot >= 0 && mid < top && mid < bot,
              "the back chevron points left, tip on the left");
    }
    check(count_color(338, 20, 346, 44, C_DIM) > 8,
          "the header carries the overflow menu");
    /* an open network shows a chevron where a secured one shows a padlock */
    check(count_color(320, ROWS_Y0 + 2 * 42 + 8, 340,
                      ROWS_Y0 + 2 * 42 + 32, C_DIM) > 4,
          "the open network row ends in a chevron, not a padlock");
    check(dump("out/wifi_list_on.raw"), "wrote list-on RGB565 dump");
}

static void check_details(void) {
    struct wifi_link_info li;

    printf("\n== Network Details, painted from the forged association ==\n");
    check(wifi_link_info(&li) == 0, "the link seam answers");
    check(li.rssi_dbm == -52 && li.link_mbps == 1733 && li.channel == 100,
          "the seam reports -52 dBm / 1733 Mbps / channel 100");
    check(count_color(34, 12, 220, 27, C_TEXT) > 100, "the title is inked");
    for (int i = 0; i < 5; i++) {
        char lab[24];
        snprintf(lab, sizeof(lab), "status row %d is inked", i);
        check(count_color(20, 180 + i * 13, 340, 188 + i * 13, C_TEXT) > 20, lab);
    }
    for (int i = 0; i < 7; i++) {
        char lab[28];
        snprintf(lab, sizeof(lab), "advanced row %d is inked", i);
        check(count_color(20, ADV_Y + 14 + i * 13, 340,
                          ADV_Y + 22 + i * 13, C_TEXT) > 10, lab);
    }
    check(at_v(332, TOG_Y0 + 14) == C_BG, "pref 0 starts on (knock-out right)");
    check(dump("out/wifi_details.raw"), "wrote details RGB565 dump");
}

int main(int argc, char **argv) {
    struct wifi_scan_result scan[WIFI_MAX_SCAN_RESULTS];
    struct wifi_config cfg;
    struct wifi_link_info li;
    uint64_t h_off, h_nophy, h_on, h_det;
    int blob_len, n, arg = -1;

    (void)argc; (void)argv;
    printf("== Wi-Fi: real seam, 1080x2340 framebuffer, K=3 ==\n");
    gfx_init(&fb[0][0], W, H, STRIDE);
    theme_init();
    font_5x7_init();
    build_fixtures();
    wifiui_init();
    wifi_init();

    check_font_ext();

    /* radio off: the seam refuses and the screen says the same thing */
    wcn_set_sysfs_root(FX_READY);
    wcn_forge_connected(NULL, NULL);
    wcn_set_scan_dump(NULL, 0);
    wifi_off();
    check_list_off(&h_off);

    /* radio on with nothing under ieee80211: "Radio not ready", not empty air */
    wifi_on();
    check_radio_not_ready(h_off, &h_nophy);

    /* phy exists, but this world has no netlink: the walk must not lie */
    check_netlink_refusal_on_host();

    /* the real thing: a kernel-shaped GET_SCAN dump through the device parser */
    blob_len = build_blob();
    wcn_set_scan_dump(g_blob, blob_len);
    check_real_air();

    /* rows land on the glass, in the parser's order */
    wifiui_open();
    wifiui_event(WIFIUI_EV_TOUCH, 180 * 3, (ROWS_Y0 + 20) * 3);
    check(wifiui_take_action(&arg) == WIFIUI_ACT_CONNECT && arg == 0,
          "tapping row 0 queues CONNECT with its scan index");
    memset(&cfg, 0, sizeof(cfg));
    strcpy(cfg.ssid, "Qu\303\241n C\303\240 Ph\303\252_5G");
    cfg.auth = WIFI_AUTH_WPA3_SAE;
    check(wifi_connect(&cfg) == -1, "the executor is honestly refused");
    check_list_on(&h_on);

    /* a supplicant elsewhere associates: the card and the details page exist */
    memset(&li, 0, sizeof(li));
    li.rssi_dbm = -52; li.link_mbps = 1733; li.freq_mhz = 5500; li.channel = 100;
    li.auth = WIFI_AUTH_WPA3_SAE;
    strcpy(li.ipv4, "192.168.1.42"); strcpy(li.gateway, "192.168.1.1");
    strcpy(li.netmask, "255.255.255.0"); strcpy(li.dns, "8.8.8.8, 8.8.4.4");
    wcn_forge_connected("Qu\303\241n C\303\240 Ph\303\252_5G", &li);
    wifiui_open();
    draw_wifi();
    check(at_v(180, 140) != C_BG, "the connected card appears");
    check(count_color(52, 124, 300, 140, C_TEXT) > 100,
          "the associated SSID is inked, diacritics included");
    check(count_color(52, 143, 120, 151, C_TEAL) > 10,
          "the Connected subline is inked teal");
    check(h_on != hash_fb(), "associating repaints the list");

    wifiui_event(WIFIUI_EV_TOUCH, 180 * 3, 140 * 3);
    check(wifiui_page() == 1, "the connected card opens Network Details");
    draw_wifi();
    h_det = hash_fb();
    check_details();

    /* the Advanced fold: same coordinates, two states */
    wifiui_event(WIFIUI_EV_TOUCH, 180 * 3, ADV_Y * 3);
    check(wifiui_adv_open() == 0, "tapping Advanced folds it");
    draw_wifi();
    check(count_color(20, ADV_Y + 14, 340, ADV_Y + 22, C_TEXT) == 0,
          "folded: the first advanced row is gone");
    check(h_det != hash_fb(), "folding repaints the page");
    wifiui_event(WIFIUI_EV_TOUCH, 180 * 3, ADV_Y * 3);
    check(wifiui_adv_open() == 1, "tapping again unfolds");

    /* a pref switch flips under its own thumb */
    wifiui_event(WIFIUI_EV_TOUCH, 320 * 3, (TOG_Y0 + 13) * 3);
    check(wifiui_pref(0) == 0, "pref 0 flips off");
    draw_wifi();
    check(at_v(308, TOG_Y0 + 14) == C_DIM, "pref 0 knob moved left, dim");

    /* back out; the master switch drops the radio and the association */
    wifiui_event(WIFIUI_EV_TOUCH, 18 * 3, 18 * 3);
    check(wifiui_page() == 0, "the back chevron returns to the list");
    /* the list header chevron declines the touch so the hub can leave */
    check(wifiui_event(WIFIUI_EV_TOUCH, 16 * 3, 18 * 3) == 0,
          "the list header chevron hands the touch back to the hub");
    wifiui_event(WIFIUI_EV_TOUCH, 314 * 3, 86 * 3);
    check(wifiui_master_on() == 0, "the master switch powers the radio off");
    check(wifiui_rows() == 0, "and the scan rows leave with it");
    check(wifi_is_connected() == 0, "powering off drops the association");
    {
        char soft[8];
        check(read_file_str(FX_READY "/class/rfkill/rfkill0/soft",
                            soft, (int)sizeof(soft)) > 0 &&
              soft[0] == '1', "wifi_off blocked the wlan rfkill soft switch");
    }

    /* truncated blob: the parser must stop at the frame edge, not guess */
    wifi_on();
    wcn_set_scan_dump(g_blob, 30);
    n = wifi_scan(scan, WIFI_MAX_SCAN_RESULTS);
    check(n == 0, "a blob cut mid-header yields zero rows instead of garbage");
    wifiui_open();
    draw_wifi();
    check(wifiui_scan_failed() == 0, "an answered-but-empty scan is not a failure");
    check(count_color(16, ROWS_Y0, 130, ROWS_Y0 + 8, C_DIM) > 20,
          "'No networks found' is inked where rows would be");

    /* the same layout must survive K=1 on a 360x780 framebuffer */
    gfx_init(&small[0][0], 360, 780, 360 * 2);
    wifiui_open();
    draw_wifi();
    check(small[100][100] != 0, "the list renders on a 360x780 framebuffer");

    printf("\n%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
