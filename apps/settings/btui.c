/* SPDX-License-Identifier: MIT */
/* apps/settings/btui.c - Bluetooth list + Bluetooth Details, 360x780 virtual.
 *
 * See btui.h. Drawn entirely through the bootui primitives so the same code
 * paints K=3 glass and the K=1 host framebuffer. The roster is bt_scan()'s;
 * nothing here hardcodes a device name. Numbers on the details page come from
 * bt_link_info(), and while it returns < 0 the row reads "no answer".
 */

#include "apps/settings/btui.h"

#include "apps/bootui/bootui.h"
#include "gui/gfx/gfx.h"
#include "hal/include/bluetooth.h"

#include <stddef.h>

/* ---- state ---------------------------------------------------------------- */

static int g_vis;
static int g_page;                  /* 0 list, 1 details */
static int g_adv = 1;
static int g_details = -1;

static struct bt_device g_scan[BT_MAX_DEVICES];
static int g_nscan;
static int g_scanfail;
static int g_paired[BT_MAX_DEVICES];
static int g_avail[BT_MAX_DEVICES];
static int g_npaired;
static int g_navail;
static int g_avail_hdr_y;
static int g_avail_y0;

static enum btui_action g_act = BTUI_ACT_NONE;
static int g_arg;

/* ---- helpers -------------------------------------------------------------- */

static int bt_isqrt(int v) {
    int lo = 0, hi = 1024;
    if (v <= 0) return 0;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (mid * mid <= v) lo = mid; else hi = mid - 1;
    }
    return lo;
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

static const char *sig_word(int rssi) {
    if (rssi >= -60) return "Excellent";
    if (rssi >= -70) return "Good";
    if (rssi >= -80) return "Fair";
    return "Poor";
}

static const char *codec_name(enum bt_codec c) {
    switch (c) {
    case BT_CODEC_SBC:  return "SBC";
    case BT_CODEC_AAC:  return "AAC";
    case BT_CODEC_LDAC: return "LDAC";
    case BT_CODEC_APTX: return "aptX";
    default:            return "N/A";
    }
}

static const char *type_word(int t) {
    switch (t) {
    case BT_DEV_HEADSET:  return "Headset";
    case BT_DEV_WATCH:    return "Watch";
    case BT_DEV_CAR:      return "Car";
    case BT_DEV_SPEAKER:  return "Speaker";
    case BT_DEV_PHONE:    return "Phone";
    case BT_DEV_KEYBOARD: return "Keyboard";
    default:              return "Device";
    }
}

/* ---- icons ---------------------------------------------------------------- */

static void disc(int cx, int cy, int r, struct umos_color c) {
    for (int dy = -r; dy <= r; dy++) {
        int dx = bt_isqrt(r * r - dy * dy);
        bui_fill(cx - dx, cy + dy, 2 * dx + 1, 1, c);
    }
}

static void line(int x0, int y0, int x1, int y1, struct umos_color c) {
    int dx = x1 - x0, dy = y1 - y0;
    int ax = dx < 0 ? -dx : dx;
    int ay = dy < 0 ? -dy : dy;
    int n = ax > ay ? ax : ay;
    if (n < 1) n = 1;
    for (int i = 0; i <= n; i++)
        bui_fill(x0 + dx * i / n, y0 + dy * i / n, 1, 1, c);
}

/* The Bluetooth rune: a stem with two right-facing triangles and the two
 * crossing strokes that make it a bind-rune rather than a bow tie. */
static void ic_bt(int x, int y, int w, int h, struct umos_color c) {
    int mid = x + w / 2;
    line(mid, y, mid, y + h, c);
    line(mid, y, x + w, y + h / 4, c);
    line(x + w, y + h / 4, mid, y + h / 2, c);
    line(mid, y + h / 2, x + w, y + 3 * h / 4, c);
    line(x + w, y + 3 * h / 4, mid, y + h, c);
    line(x, y + h / 4, x + w, y + 3 * h / 4, c);
    line(x, y + 3 * h / 4, x + w, y + h / 4, c);
}

static void ic_dev(int x, int y, int type, struct umos_color c) {
    switch (type) {
    case BT_DEV_HEADSET:
        for (int dx = -5; dx <= 5; dx++) {
            int dy = bt_isqrt(25 - dx * dx);
            if (dy >= 4) bui_fill(x + 8 + dx, y + 8 - dy, 1, 1, c);
        }
        bui_fill(x + 2, y + 6, 3, 8, c);
        bui_fill(x + 11, y + 6, 3, 8, c);
        break;
    case BT_DEV_WATCH:
        bui_round(x + 4, y + 3, 8, 10, 2, BUI_SURFACE, c);
        bui_fill(x + 6, y, 4, 3, c);
        bui_fill(x + 6, y + 13, 4, 3, c);
        break;
    case BT_DEV_CAR:
        bui_fill(x + 1, y + 8, 14, 4, c);
        bui_fill(x + 4, y + 5, 8, 3, c);
        bui_fill(x + 3, y + 12, 3, 2, BUI_BG);
        bui_fill(x + 10, y + 12, 3, 2, BUI_BG);
        break;
    case BT_DEV_SPEAKER:
        bui_round(x + 3, y + 1, 10, 14, 2, BUI_SURFACE, c);
        disc(x + 8, y + 8, 3, c);
        break;
    case BT_DEV_PHONE:
        bui_round(x + 4, y + 1, 8, 14, 2, BUI_SURFACE, c);
        bui_fill(x + 6, y + 12, 4, 1, c);
        break;
    case BT_DEV_KEYBOARD:
        bui_round(x + 1, y + 4, 14, 9, 2, BUI_SURFACE, c);
        for (int i = 0; i < 4; i++) bui_fill(x + 3 + i * 3, y + 7, 2, 1, c);
        bui_fill(x + 4, y + 10, 8, 1, c);
        break;
    default:
        bui_round(x + 3, y + 3, 10, 10, 3, BUI_SURFACE, c);
        break;
    }
}

static void ic_bars(int x, int y, struct umos_color c, int n) {
    static const int h[4] = { 5, 8, 11, 14 };
    for (int i = 0; i < 4; i++)
        bui_fill(x + i * 5, y + 14 - h[i], 3, h[i], i < n ? c : BUI_BORDER);
}

static void ic_info(int x, int y, struct umos_color c) {
    int r = 6;
    for (int dy = -r; dy <= r; dy++) {
        int dx = bt_isqrt(r * r - dy * dy);
        if (dx >= r - 1) {
            bui_fill(x + r - dx, y + r + dy, 1, 1, c);
            bui_fill(x + r + dx, y + r + dy, 1, 1, c);
        }
    }
    bui_fill(x + r, y + 2, 1, 1, c);
    bui_fill(x + r, y + 4, 1, 5, c);
}

static void ic_slash(int x, int y, struct umos_color c) {
    int r = 6;
    for (int dy = -r; dy <= r; dy++) {
        int dx = bt_isqrt(r * r - dy * dy);
        if (dx >= r - 1) {
            bui_fill(x + r - dx, y + r + dy, 1, 1, c);
            bui_fill(x + r + dx, y + r + dy, 1, 1, c);
        }
    }
    for (int i = -4; i <= 4; i++)
        bui_fill(x + r + i, y + r - i, 1, 1, c);
}

static void ic_lock(int x, int y, struct umos_color c) {
    for (int dx = -3; dx <= 3; dx++) {
        int dy = bt_isqrt(9 - dx * dx);
        if (dy >= 2) bui_fill(x + 4 + dx, y + 5 - dy, 1, 1, c);
    }
    bui_fill(x, y + 5, 9, 7, c);
}

static void ic_scan(int x, int y, struct umos_color c) {
    for (int dy = -4; dy <= 4; dy++) {
        int dx = bt_isqrt(16 - dy * dy);
        bui_fill(x + 5 - dx, y + 5 + dy, 1, 1, c);
        bui_fill(x + 5 + dx, y + 5 + dy, 1, 1, c);
    }
    for (int i = 0; i < 5; i++) bui_fill(x + 9 + i, y + 9 + i, 1, 1, c);
}

static void ic_pair(int x, int y, struct umos_color c) {
    bui_fill(x + 6, y + 1, 2, 12, c);
    bui_fill(x + 1, y + 6, 12, 2, c);
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

/* ---- layout --------------------------------------------------------------- */

#define BT_SW_X        292
#define BT_SW_Y         76
#define BT_PAIR_HDR_Y  190
#define BT_PAIR_Y0     208
#define BT_ROW_H        42
#define BT_ROW_CARD_H   36
#define BT_INFO_X      306
#define BT_ADV_Y       (180 + 5 * 13 + 8)
#define BT_ADV_ROWS     8

static int list_on(void) {
    return bt_status() == BT_STATUS_ON || bt_is_connected();
}

static void det_layout(int *adv_y, int *act_y0, int *tog_y0) {
    *adv_y = BT_ADV_Y;
    *act_y0 = g_adv ? BT_ADV_Y + 14 + BT_ADV_ROWS * 13 + 8 : BT_ADV_Y + 22;
    *tog_y0 = *act_y0 + 3 * 26 + 6;
}

static void refresh(void) {
    int rc = bt_scan(g_scan, BT_MAX_DEVICES);

    g_scanfail = rc < 0 && list_on();
    g_nscan = rc < 0 ? 0 : (rc > BT_MAX_DEVICES ? BT_MAX_DEVICES : rc);
    g_npaired = 0;
    g_navail = 0;
    for (int i = 0; i < g_nscan; i++) {
        if (g_scan[i].paired && g_npaired < BT_MAX_DEVICES)
            g_paired[g_npaired++] = i;
        else if (!g_scan[i].paired && g_navail < BT_MAX_DEVICES)
            g_avail[g_navail++] = i;
    }
    g_avail_hdr_y = BT_PAIR_Y0 + (g_npaired > 0 ? g_npaired : 1) * BT_ROW_H + 12;
    g_avail_y0 = g_avail_hdr_y + 14;
    if (g_details >= g_nscan) g_details = -1;
}

static const char *details_name(void) {
    if (g_details >= 0 && g_details < g_nscan) return g_scan[g_details].name;
    if (bt_current_name()[0]) return bt_current_name();
    return "Unknown device";
}

/* ---- list page ------------------------------------------------------------ */

static void draw_row(int ry, int idx, int paired) {
    const struct bt_device *d = &g_scan[idx];
    struct umos_color lead = d->connected ? BUI_TEAL : BUI_DIM;
    char sub[24];
    sub[0] = '\0';              /* cat() must start from a terminated buffer */

    bui_round(12, ry, 336, BT_ROW_CARD_H, 10, BUI_SURFACE, BUI_BORDER);
    ic_dev(22, ry + 10, d->type, lead);
    bui_text(d->name, 52, ry + 6, 2, BUI_TEXT);

    if (d->connected) {
        cat(sub, sizeof(sub), "Connected");
        if (d->battery != BT_BATTERY_UNKNOWN) {
            char pct[8];
            cat(sub, sizeof(sub), "  ");
            put_i(pct, sizeof(pct), d->battery);
            cat(sub, sizeof(sub), pct);
            cat(sub, sizeof(sub), "%");
        }
    } else if (paired) {
        cat(sub, sizeof(sub), "Paired  ");
        cat(sub, sizeof(sub), type_word(d->type));
    } else {
        cat(sub, sizeof(sub), type_word(d->type));
        cat(sub, sizeof(sub), "  ");
        cat(sub, sizeof(sub), codec_name((enum bt_codec)d->codec));
    }
    bui_text(sub, 52, ry + 23, 1, d->connected ? BUI_TEAL : BUI_DIM);

    if (d->connected) {
        bui_fill(292, ry + 14, 3, 8, BUI_TEAL);
        bui_fill(295, ry + 12, 3, 12, BUI_TEAL);
    }
    ic_info(BT_INFO_X + 6, ry + 10, BUI_DIM);
}

static void draw_list(void) {
    int on = list_on();

    chev_left(12, 16, BUI_TEXT);
    bui_text("Bluetooth", 34, 14, 3, BUI_TEXT);
    for (int i = 0; i < 3; i++)
        bui_fill(340, 24 + i * 6, 3, 3, BUI_DIM);

    bui_round(12, 64, 336, 44, 12, BUI_SURFACE, BUI_BORDER);
    bui_text("Bluetooth", 26, 78, 2, BUI_TEXT);
    ui_switch(BT_SW_X, BT_SW_Y, on);

    ic_bt(164, 118, 32, 54, on ? BUI_TEAL : BUI_DIM);

    if (!on) {
        bui_text("Bluetooth is off", 16, 190, 1, BUI_DIM);
        return;
    }

    bui_text("Paired Devices", 16, BT_PAIR_HDR_Y, 1, BUI_TEAL);
    if (g_npaired == 0)
        bui_text("No paired devices", 16, BT_PAIR_Y0, 1, BUI_DIM);
    for (int i = 0; i < g_npaired; i++) {
        draw_row(BT_PAIR_Y0 + i * BT_ROW_H, g_paired[i], 1);
    }

    bui_text("Available Devices", 16, g_avail_hdr_y, 1, BUI_TEAL);
    if (g_navail == 0) {
        bui_text(g_scanfail ? "Radio not ready" : "No devices found",
                 16, g_avail_y0, 1, BUI_DIM);
        return;
    }
    for (int i = 0; i < g_navail; i++)
        draw_row(g_avail_y0 + i * BT_ROW_H, g_avail[i], 0);
}

/* ---- details page --------------------------------------------------------- */

static void det_row(int y, const char *label, const char *val,
                    struct umos_color vc) {
    bui_text(label, 20, y, 1, BUI_DIM);
    bui_text(val, 20 + bui_text_w(label, 1) + 3, y, 1, vc);
}

static void draw_details(void) {
    struct bt_link_info li;
    int have = bt_link_info(&li) == 0;
    int adv_y, act_y0, tog_y0;
    char line[40];
    int di = (g_details >= 0 && g_details < g_nscan) ? g_details : -1;
    int bars = have ? bars_of(li.rssi_dbm) : 0;

    det_layout(&adv_y, &act_y0, &tog_y0);

    chev_left(12, 12, BUI_TEXT);
    bui_text("Bluetooth Details", 34, 12, 2, BUI_TEXT);
    ic_info(332, 10, BUI_DIM);
    bui_hline(0, 33, BUI_V_W, BUI_BORDER);

    ic_bt(172, 42, 16, 24, BUI_TEAL);
    bui_text_center(details_name(), 180, 74, 2, BUI_TEXT);

    for (int i = 0; i < 3; i++) {
        int x = 14 + i * 112;
        bui_round(x, 100, 104, 54, 8, BUI_SURFACE, BUI_BORDER);
        if (i == 0) ic_bars(x + 45, 106, BUI_TEAL, bars);
        else if (i == 1) ic_slash(x + 46, 106, BUI_DIM);
        else ic_lock(x + 48, 106, BUI_DIM);
    }
    bui_text_center("Signal", 66, 126, 1, BUI_DIM);
    bui_text_center("Network usage", 178, 126, 1, BUI_DIM);
    bui_text_center("Protected", 290, 126, 1, BUI_DIM);
    bui_text_center(have ? sig_word(li.rssi_dbm) : "no answer",
                    66, 138, 1, BUI_TEXT);
    bui_text_center("N/A", 178, 138, 1, BUI_TEXT);
    bui_text_center("Yes", 290, 138, 1, BUI_TEXT);

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
            lab = "Connection type:";
            if (have) cat(line, sizeof(line), li.le ? "LE" : "Classic");
            break;
        case 2:
            lab = "Frequency:";
            if (have) cat(line, sizeof(line),
                          li.freq_mhz >= 5000 ? "5 GHz" : "2.4 GHz");
            break;
        case 3:
            lab = "Audio Codec:";
            if (have) cat(line, sizeof(line), codec_name(li.codec));
            else if (di >= 0)
                cat(line, sizeof(line), codec_name((enum bt_codec)g_scan[di].codec));
            break;
        default:
            lab = "Battery:";
            if (have && li.battery >= 0) {
                put_i(line, sizeof(line), li.battery);
                cat(line, sizeof(line), " %");
            } else if (di >= 0 && g_scan[di].battery != BT_BATTERY_UNKNOWN) {
                put_i(line, sizeof(line), g_scan[di].battery);
                cat(line, sizeof(line), " %");
            }
            break;
        }
        val = line[0] ? line : "no answer";
        det_row(180 + i * 13, lab, val, BUI_TEXT);
    }

    bui_text("Advanced", 16, adv_y, 1, BUI_TEAL);
    chev_v(328, adv_y, g_adv, BUI_DIM);
    if (g_adv) {
        static const char *const lab[BT_ADV_ROWS] = {
            "Privacy:", "Spatial Mode:", "ANC mode:", "IP settings:",
            "Bluetooth MAC:", "Subnet Mask:", "Gateway:", "DNS:",
        };
        for (int i = 0; i < BT_ADV_ROWS; i++) {
            const char *val = "no answer";
            switch (i) {
            case 0: val = "Use randomized address (MAC)"; break;
            case 1: val = "Toggle"; break;
            case 2: val = "None"; break;
            case 3: val = "N/A"; break;
            case 4: if (have) val = li.mac; break;
            case 5: if (have) val = li.netmask; break;
            case 6: if (have) val = li.gateway; break;
            default: if (have) val = li.dns; break;
            }
            det_row(adv_y + 14 + i * 13, lab[i], val, BUI_TEXT);
        }
    }

    for (int i = 0; i < 3; i++) {
        int y = act_y0 + i * 26;
        if (i == 0) ic_scan(20, y + 5, BUI_DIM);
        else if (i == 1) ic_slash(20, y + 5, BUI_DIM);
        else ic_pair(20, y + 5, BUI_DIM);
        bui_text(i == 0 ? "Scan for devices"
                 : i == 1 ? "Bluetooth & Wi-Fi scanning" : "Pair new device",
                 44, y + 9, 1, BUI_TEXT);
    }

    for (int i = 0; i < BT_PROFILE_COUNT; i++) {
        int y = tog_y0 + i * 26;
        bui_text(i == 0 ? "Phone calls"
                 : i == 1 ? "Media audio"
                 : i == 2 ? "Contact sharing"
                 : i == 3 ? "Input device" : "Share data via Bluetooth",
                 20, y + 9, 1, BUI_TEXT);
        ui_switch(298, y + 4, bt_profile(i));
    }
}

/* ---- input ---------------------------------------------------------------- */

static int in_rect(int vx, int vy, int x, int y, int w, int h) {
    return vx >= x && vx < x + w && vy >= y && vy < y + h;
}

static void queue(enum btui_action a, int arg) {
    g_act = a;
    g_arg = arg;
}

static int open_details(int index) {
    g_details = index;
    g_page = 1;
    return 1;
}

static int touch_row_group(int vx, int vy, int y0, const int *idx, int n) {
    for (int i = 0; i < n; i++) {
        int ry = y0 + i * BT_ROW_H;
        if (!in_rect(vx, vy, 12, ry, 336, BT_ROW_CARD_H)) continue;
        if (in_rect(vx, vy, BT_INFO_X, ry, 36, BT_ROW_CARD_H))
            return open_details(idx[i]);
        if (g_scan[idx[i]].connected) return open_details(idx[i]);
        queue(g_scan[idx[i]].paired ? BTUI_ACT_CONNECT : BTUI_ACT_PAIR, idx[i]);
        return 1;
    }
    return 0;
}

static int touch_list(int vx, int vy) {
    /* the header chevron leaves the screen; the hub takes the BACK event */
    if (in_rect(vx, vy, 4, 4, 28, 28)) return 0;
    if (in_rect(vx, vy, BT_SW_X - 8, BT_SW_Y - 8, 44 + 16, 20 + 16)) {
        if (list_on()) bt_off(); else bt_on();
        refresh();
        return 1;
    }
    if (!list_on()) return 0;
    if (touch_row_group(vx, vy, BT_PAIR_Y0, g_paired, g_npaired)) return 1;
    return touch_row_group(vx, vy, g_avail_y0, g_avail, g_navail);
}

static int touch_details(int vx, int vy) {
    int adv_y, act_y0, tog_y0;

    det_layout(&adv_y, &act_y0, &tog_y0);
    if (in_rect(vx, vy, 4, 4, 28, 28)) { g_page = 0; return 1; }
    if (in_rect(vx, vy, 12, adv_y - 5, 336, 22)) { g_adv = !g_adv; return 1; }
    for (int i = 0; i < 3; i++) {
        if (!in_rect(vx, vy, 12, act_y0 + i * 26, 336, 26)) continue;
        queue(i == 0 ? BTUI_ACT_SCAN
              : i == 1 ? BTUI_ACT_SCAN_SETTINGS : BTUI_ACT_PAIR_NEW, 0);
        return 1;
    }
    for (int i = 0; i < BT_PROFILE_COUNT; i++) {
        if (!in_rect(vx, vy, 290, tog_y0 + i * 26, 60, 26)) continue;
        bt_set_profile(i, !bt_profile(i));
        return 1;
    }
    return 0;
}

/* ---- public --------------------------------------------------------------- */

void btui_init(void) {
    g_vis = 0;
    g_page = 0;
    g_adv = 1;
    g_details = -1;
    g_act = BTUI_ACT_NONE;
    refresh();
}

void btui_open(void) {
    g_vis = 1;
    g_page = 0;
    refresh();
}

void btui_close(void) { g_vis = 0; }

int btui_visible(void) { return g_vis; }

void btui_draw(void) {
    if (!g_vis) return;
    bui_begin_overlay();
    bui_view_fit();
    if (g_page == 0) draw_list(); else draw_details();
    bui_end();
}

int btui_event(uint32_t type, uint32_t d1, uint32_t d2) {
    int vx, vy;

    if (!g_vis) return 0;
    if (type == (uint32_t)BTUI_EV_BACK) {
        if (g_page) { g_page = 0; return 1; }
        return 0;
    }
    if (type != (uint32_t)BTUI_EV_TOUCH) return 0;
    bui_map_touch((int)d1, (int)d2, &vx, &vy);
    return g_page == 0 ? touch_list(vx, vy) : touch_details(vx, vy);
}

enum btui_action btui_take_action(int *arg) {
    enum btui_action a = g_act;
    if (arg) *arg = g_arg;
    g_act = BTUI_ACT_NONE;
    g_arg = 0;
    return a;
}

int btui_page(void) { return g_page; }
int btui_master_on(void) { return list_on(); }
int btui_adv_open(void) { return g_adv; }
int btui_profile(int i) { return bt_profile(i); }
int btui_rows(void) { return list_on() ? g_nscan : 0; }
int btui_paired_rows(void) { return list_on() ? g_npaired : 0; }
int btui_scan_failed(void) { return list_on() ? g_scanfail : 0; }
int btui_details_index(void) { return g_details; }
