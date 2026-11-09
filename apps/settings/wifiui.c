/* SPDX-License-Identifier: MIT */
/* apps/settings/wifiui.c - Wi-Fi settings screens: network list + details.
 *
 * See wifiui.h. Everything is drawn in the 360x780 virtual space through the
 * bootui primitives, so the same code paints K=3 glass and the K=1 host gate.
 * Numbers on the details page are the radio's own: wifi_link_info() supplies
 * them, and while it returns < 0 the row says "no answer" rather than print a
 * figure the board never reported.
 */

#include "apps/settings/wifiui.h"

#include "apps/bootui/bootui.h"
#include "gui/gfx/gfx.h"
#include "hal/include/wifi.h"

#include <stddef.h>

/* ---- state ---------------------------------------------------------------- */

static int g_vis;
static int g_page;                 /* 0 list, 1 details */
static int g_adv = 1;
static int g_prefs[3] = { 1, 1, 0 };

static struct wifi_scan_result g_scan[WIFI_MAX_SCAN_RESULTS];
static int g_nscan;
static int g_scanfail;               /* 1 when the radio refused to answer */
static char g_conn[WIFI_MAX_SSID + 1];
static int g_conn_rssi = -100;

static enum wifiui_action g_act = WIFIUI_ACT_NONE;
static int g_arg;

/* ---- small helpers -------------------------------------------------------- */

static int wi_isqrt(int v) {
    int lo = 0, hi = 1024;
    if (v <= 0) return 0;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (mid * mid <= v) lo = mid; else hi = mid - 1;
    }
    return lo;
}

static int streq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static void cat(char *out, size_t cap, const char *s) {
    size_t n = 0;
    while (n + 1 < cap && out[n]) n++;
    while (*s && n + 1 < cap) out[n++] = *s++;
    out[n] = '\0';
}

static void put_i(char *out, size_t cap, int v) {
    char tmp[12];
    int t = 0;
    size_t n = 0;
    unsigned u;

    out[0] = '\0';
    if (cap < 2) return;
    if (v < 0) { out[n++] = '-'; u = (unsigned)(-(v + 1)) + 1u; }
    else u = (unsigned)v;
    do { tmp[t++] = (char)('0' + u % 10); u /= 10; } while (u);
    while (t > 0 && n + 1 < cap) out[n++] = tmp[--t];
    out[n] = '\0';
}

static int bars_of(int rssi) {
    if (rssi >= -60) return 4;
    if (rssi >= -70) return 3;
    if (rssi >= -80) return 2;
    return 1;
}

static const char *auth_name(enum wifi_auth a) {
    switch (a) {
    case WIFI_AUTH_OPEN:     return "Open";
    case WIFI_AUTH_WPA_PSK:  return "WPA-Personal";
    case WIFI_AUTH_WPA2_PSK: return "WPA2";
    case WIFI_AUTH_WPA3_SAE: return "WPA3";
    }
    return "Open";
}

static const char *auth_long(enum wifi_auth a) {
    switch (a) {
    case WIFI_AUTH_OPEN:     return "Open";
    case WIFI_AUTH_WPA_PSK:  return "WPA-Personal";
    case WIFI_AUTH_WPA2_PSK: return "WPA2-Personal";
    case WIFI_AUTH_WPA3_SAE: return "WPA3-Personal";
    }
    return "Open";
}

static const char *sig_word(int rssi) {
    if (rssi >= -60) return "Excellent";
    if (rssi >= -70) return "Good";
    if (rssi >= -80) return "Fair";
    return "Poor";
}

/* ---- icons (integer arcs, no floats) -------------------------------------- */

static void disc(int cx, int cy, int r, struct umos_color c) {
    for (int dy = -r; dy <= r; dy++) {
        int dx = wi_isqrt(r * r - dy * dy);
        bui_fill(cx - dx, cy + dy, 2 * dx + 1, 1, c);
    }
}

/* Three 90-degree arcs plus the dot; arcs beyond the reported level stay as
 * dim outlines so a weak signal reads as weak, not as a different icon. */
static void ic_wifi(int x, int y, int s, struct umos_color c, int bars) {
    int cx = x + s / 2, cy = y + s - 1;
    int arcs = bars >= 3 ? 3 : bars >= 2 ? 2 : bars >= 1 ? 1 : 0;
    int t = s / 8 > 1 ? s / 8 : 1;

    for (int a = 1; a <= 3; a++) {
        int r = s * a / 4;
        struct umos_color col = a <= arcs ? c : BUI_BORDER;
        for (int dx = -r; dx <= r; dx++) {
            int dy = wi_isqrt(r * r - dx * dx);
            if (dy < r - t || dx > dy || -dx > dy) continue;
            bui_fill(cx + dx, cy - dy, 1, 1, col);
        }
    }
    if (bars > 0)
        bui_fill(cx, cy - 1, 2, 2, c);
}

static void ic_bars(int x, int y, struct umos_color c, int n) {
    static const int h[4] = { 5, 8, 11, 14 };
    for (int i = 0; i < 4; i++)
        bui_fill(x + i * 5, y + 14 - h[i], 3, h[i], i < n ? c : BUI_BORDER);
}

static void ic_lock(int x, int y, struct umos_color c) {
    for (int dx = -3; dx <= 3; dx++) {
        int dy = wi_isqrt(9 - dx * dx);
        if (dy >= 2) bui_fill(x + 4 + dx, y + 5 - dy, 1, 1, c);
    }
    bui_fill(x, y + 5, 9, 7, c);
}

static void ic_slash(int x, int y, struct umos_color c) {
    int r = 6;
    for (int dy = -r; dy <= r; dy++) {
        int dx = wi_isqrt(r * r - dy * dy);
        if (dx >= r - 1) {
            bui_fill(x + r - dx, y + r + dy, 1, 1, c);
            bui_fill(x + r + dx, y + r + dy, 1, 1, c);
        }
    }
    for (int i = -4; i <= 4; i++)
        bui_fill(x + r + i, y + r - i, 1, 1, c);
}

static void ui_switch(int x, int y, int on) {
    int w = 44, h = 20;
    bui_round(x, y, w, h, h / 2, on ? BUI_TEAL : BUI_SURFACE_HI,
              on ? BUI_TEAL : BUI_BORDER);
    disc(on ? x + w - h / 2 : x + h / 2, y + h / 2, h / 2 - 3,
         on ? BUI_BG : BUI_DIM);
}

/* The header back arrow: a "<" whose tip points LEFT, the way the reference
 * sheet draws it. The tip is the single pixel on the middle row and it sits
 * on the left column; the two arms open out to the right above and below. */
static void chev_left(int x, int y, struct umos_color c) {
    for (int i = 0; i < 7; i++) {
        int dx = (i <= 3 ? 3 - i : i - 3);
        bui_fill(x + dx, y + i, 1, 1, c);
    }
}

static void chev_v(int x, int y, int up, struct umos_color c) {
    for (int i = 0; i < 5; i++) {
        int dy = up ? i : 4 - i;
        bui_fill(x + i, y + dy, 1, 1, c);
        bui_fill(x + 8 - i, y + dy, 1, 1, c);
    }
}

static void ic_pencil(int x, int y, struct umos_color c) {
    for (int i = 0; i < 9; i++)
        bui_fill(x + 9 - i, y + i, 2, 1, c);
    bui_fill(x, y + 10, 3, 1, c);
}

static void ic_info(int x, int y, struct umos_color c) {
    int r = 6;
    for (int dy = -r; dy <= r; dy++) {
        int dx = wi_isqrt(r * r - dy * dy);
        if (dx >= r - 1) {
            bui_fill(x + r - dx, y + r + dy, 1, 1, c);
            bui_fill(x + r + dx, y + r + dy, 1, 1, c);
        }
    }
    bui_fill(x + r, y + 2, 1, 1, c);
    bui_fill(x + r, y + 4, 1, 5, c);
}

/* ---- layout --------------------------------------------------------------- */

#define LIST_SW_X   292
#define LIST_SW_Y   76
#define LIST_CONN_Y 116
#define LIST_CONN_H 48
#define ROW_H       42

static int list_on(void) {
    return wifi_status() == WIFI_STATUS_ON || wifi_is_connected();
}

static int list_header_y(void) { return g_conn[0] ? 176 : 124; }
static int list_rows_y0(void)  { return list_header_y() + 14; }

static void det_layout(int *adv_y, int *act_y0, int *tog_y0) {
    int ay = 180 + 5 * 13 + 8;
    *adv_y = ay;
    *act_y0 = g_adv ? ay + 14 + 7 * 13 + 8 : ay + 22;
    *tog_y0 = *act_y0 + 3 * 26 + 6;
}

/* ---- seam ----------------------------------------------------------------- */

static void refresh(void) {
    const char *cur;
    int rc = wifi_scan(g_scan, WIFI_MAX_SCAN_RESULTS);

    /* A radio that answers 0 rows and one that refuses are different facts:
     * the first is an empty room, the second means this boot has no scan. */
    g_scanfail = rc < 0 && list_on();
    g_nscan = rc < 0 ? 0 : rc;
    if (g_nscan > WIFI_MAX_SCAN_RESULTS) g_nscan = WIFI_MAX_SCAN_RESULTS;
    g_conn[0] = '\0';
    g_conn_rssi = -100;
    if (!wifi_is_connected()) return;
    cur = wifi_current_ssid();
    for (size_t i = 0; i < WIFI_MAX_SSID && cur[i]; i++) g_conn[i] = cur[i];
    for (int i = 0; i < g_nscan; i++)
        if (streq(g_scan[i].ssid, g_conn)) g_conn_rssi = g_scan[i].rssi;
}

/* ---- list page ------------------------------------------------------------ */

/* The reference sheet opens with a back chevron on the left and a kebab menu
 * on the right, both in the header. Wi-Fi and Bluetooth agree on that, so the
 * header is drawn the same way on both screens. */
static void draw_header(const char *title, int scale) {
    chev_left(12, 14, BUI_TEXT);
    bui_text(title, 34, 12, scale, BUI_TEXT);
    for (int i = 0; i < 3; i++)
        bui_fill(340, 24 + i * 6, 3, 3, BUI_DIM);
}

static void draw_list(void) {
    int on = list_on();

    draw_header("Wi-Fi", 3);

    bui_round(12, 64, 336, 44, 12, BUI_SURFACE, BUI_BORDER);
    bui_text("Wi-Fi", 26, 78, 2, BUI_TEXT);
    ui_switch(LIST_SW_X, LIST_SW_Y, on);

    if (g_conn[0]) {
        int bars = bars_of(g_conn_rssi);
        bui_round(12, LIST_CONN_Y, 336, LIST_CONN_H, 10, BUI_SURFACE, BUI_BORDER);
        ic_wifi(24, LIST_CONN_Y + 13, 22, BUI_TEAL, bars);
        bui_text(g_conn, 52, LIST_CONN_Y + 8, 2, BUI_TEXT);
        bui_text("Connected", 52, LIST_CONN_Y + 27, 1, BUI_TEAL);
        ic_bars(298, LIST_CONN_Y + 15, BUI_TEAL, bars);
        /* the reference taps through the connected card into its details */
        bui_chevron(324, LIST_CONN_Y + 19, BUI_DIM);
    }

    if (!on) {
        bui_text("Wi-Fi is off", 16, 130, 1, BUI_DIM);
        return;
    }

    bui_text("Available Networks", 16, list_header_y(), 1, BUI_TEAL);
    if (g_nscan == 0) {
        bui_text(g_scanfail ? "Radio not ready" : "No networks found",
                 16, list_rows_y0(), 1, BUI_DIM);
        return;
    }
    for (int i = 0; i < g_nscan; i++) {
        int ry = list_rows_y0() + i * ROW_H;
        int conn = g_conn[0] && streq(g_scan[i].ssid, g_conn);
        int bars = bars_of(g_scan[i].rssi);

        bui_round(12, ry, 336, ROW_H - 6, 10, BUI_SURFACE, BUI_BORDER);
        ic_wifi(24, ry + 11, 20, conn ? BUI_TEAL : BUI_DIM, bars);
        bui_text(g_scan[i].ssid, 52, ry + 6, 2, BUI_TEXT);
        bui_text(conn ? "Connected" : auth_name(g_scan[i].auth),
                 52, ry + 23, 1, BUI_DIM);
        ic_bars(296, ry + 10, conn ? BUI_TEAL : BUI_DIM, bars);
        /* The reference puts a padlock on a secured network and a chevron on
         * an open one, so the two never sit side by side in the same slot. */
        if (g_scan[i].auth != WIFI_AUTH_OPEN)
            ic_lock(326, ry + 12, BUI_DIM);
        else
            bui_chevron(326, ry + 12, BUI_DIM);
    }
}

/* ---- details page --------------------------------------------------------- */

static void det_row(int y, const char *label, const char *val,
                    struct umos_color vc) {
    bui_text(label, 20, y, 1, BUI_DIM);
    bui_text(val, 20 + bui_text_w(label, 1) + 3, y, 1, vc);
}

static void draw_details(void) {
    struct wifi_link_info li;
    int have = wifi_link_info(&li) == 0;
    int adv_y, act_y0, tog_y0;
    char line[40];

    det_layout(&adv_y, &act_y0, &tog_y0);

    chev_left(12, 12, BUI_TEXT);
    bui_text("Network Details", 34, 12, 2, BUI_TEXT);
    ic_pencil(308, 10, BUI_DIM);
    ic_info(332, 10, BUI_DIM);
    bui_hline(0, 33, BUI_V_W, BUI_BORDER);

    ic_wifi(168, 42, 24, BUI_TEAL, have ? bars_of(li.rssi_dbm) : 0);
    bui_text_center(g_conn, 180, 74, 2, BUI_TEXT);

    /* three stat tiles */
    for (int i = 0; i < 3; i++) {
        int x = 14 + i * 112;
        bui_round(x, 100, 104, 54, 8, BUI_SURFACE, BUI_BORDER);
        if (i == 0)
            ic_bars(x + 45, 106, BUI_TEAL, have ? bars_of(li.rssi_dbm) : 0);
        else if (i == 1)
            ic_slash(x + 46, 106, BUI_DIM);
        else
            ic_lock(x + 48, 106, BUI_DIM);
    }
    bui_text_center("Signal", 66, 126, 1, BUI_DIM);
    bui_text_center("Network usage", 178, 126, 1, BUI_DIM);
    bui_text_center("Security", 290, 126, 1, BUI_DIM);
    bui_text_center(have ? sig_word(li.rssi_dbm) : "no answer", 66, 138, 1, BUI_TEXT);
    bui_text_center("Not measured", 178, 138, 1, BUI_TEXT);
    bui_text_center(have ? auth_long(li.auth) : "no answer", 290, 138, 1, BUI_TEXT);

    bui_text("Status", 16, 166, 1, BUI_TEAL);
    for (int i = 0; i < 5; i++) {
        const char *lab;
        const char *val;

        line[0] = '\0';
        switch (i) {
        case 0:
            lab = "Signal strength:";
            if (have) { put_i(line, sizeof(line), li.rssi_dbm);
                        cat(line, sizeof(line), " dBm"); }
            break;
        case 1:
            lab = "Link speed:";
            if (have) { put_i(line, sizeof(line), li.link_mbps);
                        cat(line, sizeof(line), " Mbps"); }
            break;
        case 2:
            lab = "Frequency:";
            if (have)
                cat(line, sizeof(line),
                    li.freq_mhz >= 5000 ? "5 GHz" : "2.4 GHz");
            break;
        case 3:
            lab = "Channel:";
            if (have) put_i(line, sizeof(line), li.channel);
            break;
        default:
            lab = "Security:";
            if (have) cat(line, sizeof(line), auth_long(li.auth));
            break;
        }
        val = line[0] ? line : "no answer";
        det_row(180 + i * 13, lab, val, BUI_TEXT);
    }

    bui_text("Advanced", 16, adv_y, 1, BUI_TEAL);
    chev_v(328, adv_y, g_adv, BUI_DIM);
    if (g_adv) {
        static const char *const lab[7] = {
            "Privacy:", "Metered:", "IP settings:", "IPv4 address:",
            "Gateway:", "Subnet mask:", "DNS:",
        };
        for (int i = 0; i < 7; i++) {
            const char *val = "no answer";
            switch (i) {
            case 0: val = "Use randomized MAC (default)"; break;
            case 1: val = "Detect automatically"; break;
            case 2: val = "DHCP"; break;
            case 3: if (have) val = li.ipv4; break;
            case 4: if (have) val = li.gateway; break;
            case 5: if (have) val = li.netmask; break;
            default: if (have) val = li.dns; break;
            }
            det_row(adv_y + 14 + i * 13, lab[i], val, BUI_TEXT);
        }
    }

    for (int i = 0; i < 3; i++) {
        int y = act_y0 + i * 26;
        if (i == 0) ic_slash(20, y + 6, BUI_DIM);
        else if (i == 1) ic_info(22, y + 6, BUI_DIM);
        else ic_lock(21, y + 6, BUI_DIM);
        bui_text(i == 0 ? "Manage saved networks"
                 : i == 1 ? "Scan settings" : "Network preferences",
                 44, y + 10, 1, BUI_TEXT);
    }

    for (int i = 0; i < 3; i++) {
        int y = tog_y0 + i * 26;
        bui_text(i == 0 ? "Turn on Wi-Fi automatically"
                 : i == 1 ? "Open network notification" : "Wi-Fi direct",
                 20, y + 9, 1, BUI_TEXT);
        ui_switch(298, y + 4, g_prefs[i]);
    }
}

/* ---- input ---------------------------------------------------------------- */

static int in_rect(int vx, int vy, int x, int y, int w, int h) {
    return vx >= x && vx < x + w && vy >= y && vy < y + h;
}

static void queue(enum wifiui_action a, int arg) {
    g_act = a;
    g_arg = arg;
}

static int touch_list(int vx, int vy) {
    /* the header chevron leaves the screen; the hub takes the BACK event */
    if (in_rect(vx, vy, 4, 4, 28, 28)) return 0;
    if (in_rect(vx, vy, LIST_SW_X - 8, LIST_SW_Y - 8, 44 + 16, 20 + 16)) {
        if (list_on()) wifi_off(); else wifi_on();
        refresh();
        return 1;
    }
    if (g_conn[0] && in_rect(vx, vy, 12, LIST_CONN_Y, 336, LIST_CONN_H)) {
        g_page = 1;
        return 1;
    }
    if (!list_on()) return 0;
    for (int i = 0; i < g_nscan; i++) {
        int ry = list_rows_y0() + i * ROW_H;
        if (!in_rect(vx, vy, 12, ry, 336, ROW_H - 6)) continue;
        if (g_conn[0] && streq(g_scan[i].ssid, g_conn)) g_page = 1;
        else queue(WIFIUI_ACT_CONNECT, i);
        return 1;
    }
    return 0;
}

static int touch_details(int vx, int vy) {
    int adv_y, act_y0, tog_y0;

    det_layout(&adv_y, &act_y0, &tog_y0);
    if (in_rect(vx, vy, 4, 4, 28, 28)) { g_page = 0; return 1; }
    if (in_rect(vx, vy, 12, adv_y - 5, 336, 22)) { g_adv = !g_adv; return 1; }
    for (int i = 0; i < 3; i++) {
        if (!in_rect(vx, vy, 12, act_y0 + i * 26, 336, 26)) continue;
        queue(i == 0 ? WIFIUI_ACT_MANAGE_SAVED
              : i == 1 ? WIFIUI_ACT_SCAN_SETTINGS : WIFIUI_ACT_NET_PREFS, 0);
        return 1;
    }
    for (int i = 0; i < 3; i++) {
        if (!in_rect(vx, vy, 290, tog_y0 + i * 26, 60, 26)) continue;
        g_prefs[i] = !g_prefs[i];
        return 1;
    }
    return 0;
}

/* ---- public --------------------------------------------------------------- */

void wifiui_init(void) {
    g_vis = 0;
    g_page = 0;
    g_adv = 1;
    g_prefs[0] = 1;
    g_prefs[1] = 1;
    g_prefs[2] = 0;
    g_act = WIFIUI_ACT_NONE;
    refresh();
}

void wifiui_open(void) {
    g_vis = 1;
    g_page = 0;
    refresh();
}

void wifiui_close(void) { g_vis = 0; }

int wifiui_visible(void) { return g_vis; }

void wifiui_draw(void) {
    if (!g_vis) return;
    /* A sheet, not a page: the blurred wallpaper the compositor just drew is
     * what this screen sits on, so the plate is laid over it instead of
     * repainting the panel. */
    bui_begin_overlay();
    bui_view_fit();
    if (g_page == 0) draw_list(); else draw_details();
    bui_end();
}

int wifiui_event(uint32_t type, uint32_t d1, uint32_t d2) {
    int vx, vy;

    if (!g_vis) return 0;
    if (type == (uint32_t)WIFIUI_EV_BACK) {
        if (g_page) { g_page = 0; return 1; }
        return 0;
    }
    if (type != (uint32_t)WIFIUI_EV_TOUCH) return 0;
    bui_map_touch((int)d1, (int)d2, &vx, &vy);
    return g_page == 0 ? touch_list(vx, vy) : touch_details(vx, vy);
}

enum wifiui_action wifiui_take_action(int *arg) {
    enum wifiui_action a = g_act;
    if (arg) *arg = g_arg;
    g_act = WIFIUI_ACT_NONE;
    g_arg = 0;
    return a;
}

int wifiui_page(void) { return g_page; }
int wifiui_master_on(void) { return list_on(); }
int wifiui_adv_open(void) { return g_adv; }
int wifiui_pref(int i) { return i >= 0 && i < 3 ? g_prefs[i] : 0; }
int wifiui_rows(void) { return list_on() ? g_nscan : 0; }
int wifiui_scan_failed(void) { return list_on() ? g_scanfail : 0; }
