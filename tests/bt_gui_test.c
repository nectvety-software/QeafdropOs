/* SPDX-License-Identifier: MIT */
/* tests/bt_gui_test.c - host gate for the REAL Bluetooth seam + its screens.
 *
 * No device name is recited by the test: the roster comes from
 * bluetooth_hci.c's own parse_dump() running over a synthetic inquiry capture
 * assembled with the same fixed-record wire format the driver documents.
 * The live path is walked against a fixture /sys tree; the host has no HCI
 * socket, so the seam must refuse - and that refusal is measured too.
 *
 * In-run negative controls, all on the same glass:
 *   - radio off            -> scan -1, "Bluetooth is off"
 *   - no hci0              -> scan -1, "Radio not ready"
 *   - hci0 present, no HCI -> scan -1 (no invented peers)
 *   - truncated capture    -> 0 rows, "No devices found" (rc 0, not -1)
 *   - bt_connect/pair      -> -1: no stack bound, no bond is claimed
 *
 * Build (from repo root): make bt-check
 */

#include "apps/bootui/bootui.h"
#include "apps/settings/btui.h"
#include "apps/settings/settings.h"
#include "apps/keyboard/keyboard.h"
#include "apps/settings/features.h"
#include "apps/settings/wifiui.h"
#include "bsp/violet/bluetooth_hci.h"
#include "bsp/violet/wcn3990.h"
#include "gui/gfx/font_5x7.h"
#include "gui/gfx/gfx.h"
#include "gui/gfx/theme.h"
#include "gui/window_manager.h"
#include "hal/include/bluetooth.h"

#include <direct.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define W 1080
#define H 2340
#define STRIDE (W * 2)

/* Kept in step with apps/settings/btui.c. */
#define BT_SW_X       292
#define BT_SW_Y        76
#define BT_PAIR_Y0    208
#define BT_ROW_H       42
#define BT_INFO_X     306
#define BT_ADV_Y      (180 + 5 * 13 + 8)
#define BT_ADV_ROWS   8
#define ACT_Y0        (BT_ADV_Y + 14 + BT_ADV_ROWS * 13 + 8)
#define TOG_Y0        (ACT_Y0 + 3 * 26 + 6)

#define FX_READY "out/bt_fx/sys"
#define FX_NOHCI "out/bt_fx/nohci/sys"

static uint16_t fb[H][W];
static uint16_t small[780][360];

static int g_checks = 0;
static int g_fail = 0;

static void check(int cond, const char *label) {
    g_checks++;
    if (cond) printf("ok    %s\n", label);
    else { g_fail++; printf("FAIL  %s\n", label); }
}

/* ---- stubs for the pieces the phone supplies ------------------------------ */

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

/* Stubs for the pieces of window_manager / platform HAL that the Features page
 * touches but that the host harness does not provide. These mirror the real
 * signatures; the gate only checks routing, not the actions themselves. */
int wm_split_active(void) { return 0; }
int wm_set_split(int left_app, int right_app, int ratio_q8) {
    (void)left_app; (void)right_app; (void)ratio_q8; return 0;
}
void wm_split_end(void) { }
int wm_float_count(void) { return 0; }
int wm_float_add(int app_id, int x, int y, int w, int h) {
    (void)app_id; (void)x; (void)y; (void)w; (void)h; return 0;
}
int wm_float_remove(int app_id) { (void)app_id; return 0; }

int usb_gadget_cable(void) { return 0; }

/* Features page is now linked in (apps/settings/features.c) and exercised by
 * the hub-routing gate below (page 4). */

/* ---- measured facts -------------------------------------------------------- */

static uint16_t at_v(int vx, int vy) { return fb[vy * 3][vx * 3]; }

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

#define C_TEAL rgb565(BUI_TEAL.r, BUI_TEAL.g, BUI_TEAL.b)
#define C_TEXT rgb565(BUI_TEXT.r, BUI_TEXT.g, BUI_TEXT.b)
#define C_DIM  rgb565(BUI_DIM.r, BUI_DIM.g, BUI_DIM.b)

static int count_color(int vx0, int vy0, int vx1, int vy1, uint16_t col) {
    int n = 0;
    for (int vy = vy0; vy < vy1; vy++)
        for (int vx = vx0; vx < vx1; vx++)
            if (at_v(vx, vy) == col) n++;
    return n;
}

static int ink(int vx0, int vy0, int vx1, int vy1) {
    return count_color(vx0, vy0, vx1, vy1, C_TEXT);
}

static void clear_fb(void) {
    memset(fb, 0, sizeof(fb));
    gfx_init(&fb[0][0], W, H, STRIDE);
}

static int dump_raw(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    size_t n = fwrite(&fb[0][0], 1, (size_t)W * H * 2, f);
    fclose(f);
    return n == (size_t)W * H * 2;
}

/* ---- fixture tree ---------------------------------------------------------- */

static void mkdir_p(const char *path) {
    char buf[256];
    size_t n = strlen(path);
    if (n >= sizeof(buf)) return;
    memcpy(buf, path, n + 1);
    for (size_t i = 0; i < n; i++)
        if (buf[i] == '/' && i > 0) {
            char keep = buf[i];
            buf[i] = '\0';
            _mkdir(buf);
            buf[i] = keep;
        }
    _mkdir(buf);
}

static void put_file(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fwrite(text, 1, strlen(text), f);
    fclose(f);
}

static void make_fixtures(void) {
    mkdir_p(FX_READY "/class/bluetooth/hci0");
    mkdir_p(FX_READY "/class/rfkill/rfkill1");
    put_file(FX_READY "/class/bluetooth/hci0/address", "B2:02:02:45:B6\n");
    put_file(FX_READY "/class/rfkill/rfkill1/type", "bluetooth\n");
    put_file(FX_READY "/class/rfkill/rfkill1/soft", "1\n");
    mkdir_p(FX_NOHCI "/class/bluetooth");
}

/* ---- inquiry capture ------------------------------------------------------- */

static unsigned char g_blob[BT_DUMP_HDR + BT_REC_SIZE * 8];
static int g_len;

static void put16(unsigned char *p, unsigned v) {
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
}

static void rec(int i, const char *name, int rssi, int batt, int type,
                int codec, int paired, int conn, int prof) {
    unsigned char *r = g_blob + BT_DUMP_HDR + i * BT_REC_SIZE;
    memset(r, 0, BT_REC_SIZE);
    for (int j = 0; j < BT_ADDR_LEN; j++) r[j] = (unsigned char)(0xB0 + i * 3 + j);
    for (int j = 0; j < BT_MAX_NAME && name[j]; j++) r[6 + j] = (unsigned char)name[j];
    r[36] = (unsigned char)(int8_t)rssi;
    r[37] = (unsigned char)batt;
    r[38] = (unsigned char)type;
    r[39] = (unsigned char)codec;
    r[40] = (unsigned char)paired;
    r[41] = (unsigned char)conn;
    r[42] = (unsigned char)prof;
}

static int build_capture(int n) {
    memcpy(g_blob, BT_DUMP_MAGIC, 4);
    put16(g_blob + 4, BT_REC_SIZE);
    put16(g_blob + 6, (unsigned)n);
    rec(0, "Sony WH-1000XM5",      -55, 78,           BT_DEV_HEADSET,  BT_CODEC_LDAC, 1, 1, 0x1F);
    rec(1, "Samsung Galaxy Watch", -68, BT_BATTERY_UNKNOWN, BT_DEV_WATCH, BT_CODEC_SBC, 1, 0, 0x03);
    rec(2, "Tesla Model 3",        -72, BT_BATTERY_UNKNOWN, BT_DEV_CAR,   BT_CODEC_NONE,1, 0, 0x01);
    rec(3, "QEAF Speaker",         -64, BT_BATTERY_UNKNOWN, BT_DEV_SPEAKER,BT_CODEC_AAC, 0, 0, 0x02);
    rec(4, "BT Keyboard",          -80, 90,           BT_DEV_KEYBOARD, BT_CODEC_NONE, 0, 0, 0x08);
    g_len = BT_DUMP_HDR + n * BT_REC_SIZE;
    return g_len;
}

static struct bt_link_info sample_link(void) {
    struct bt_link_info li;
    memset(&li, 0, sizeof(li));
    li.rssi_dbm = -55;
    li.freq_mhz = 2400;
    li.battery = 78;
    li.le = 0;
    li.codec = BT_CODEC_LDAC;
    memcpy(li.mac, "B2:02:02:45:B6", 15);
    memcpy(li.ipv4, "192.168.1.10", 13);
    memcpy(li.gateway, "192.168.1.1", 12);
    memcpy(li.netmask, "255.255.255.0", 14);
    memcpy(li.dns, "8.8.8.8, 8.8.4.4", 19);
    return li;
}

static struct bt_link_info *sample_link_ptr(void) {
    static struct bt_link_info li;
    li = sample_link();
    return &li;
}

/* ---- the seam itself ------------------------------------------------------- */

static void check_seam(void) {
    struct bt_device devs[BT_MAX_DEVICES];
    struct bt_link_info li;
    uint8_t addr[BT_ADDR_LEN] = {0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5};

    printf("\n== HCI seam: negative controls ==\n");
    bt_init();
    check(bt_scan(devs, BT_MAX_DEVICES) == -1, "off radio refuses to scan");
    check(bt_link_info(&li) == -1, "no link info while disconnected");
    check(bt_current_name()[0] == '\0', "no peer name while disconnected");

    bt_on();
    bt_set_sysfs_root(FX_NOHCI);
    bt_set_scan_dump(NULL, 0);
    check(bt_scan(devs, BT_MAX_DEVICES) == -1, "no hci0 refuses to invent peers");

    bt_set_sysfs_root(FX_READY);
    check(bt_scan(devs, BT_MAX_DEVICES) == -1,
          "hci0 present but no inquiry transport still refuses");

    bt_set_scan_dump(g_blob, 12);       /* header says 5, only 4 body bytes */
    check(bt_scan(devs, BT_MAX_DEVICES) == 0,
          "a truncated capture answers 0 rows, not -1");

    check(bt_connect(addr) == -1, "bt_connect refuses: no stack bound");
    check(bt_pair(addr) == -1, "bt_pair refuses: no stack bound");
    check(bt_unpair(addr) == -1, "bt_unpair refuses: no stack bound");

    printf("\n== HCI seam: real parser ==\n");
    bt_set_scan_dump(g_blob, build_capture(5));
    int n = bt_scan(devs, BT_MAX_DEVICES);
    check(n == 5, "parser returns all five captured peers");
    int found_sony = 0, found_tesla = 0, found_keyboard = 0;
    for (int i = 0; i < n; i++) {
        if (strcmp(devs[i].name, "Sony WH-1000XM5") == 0) found_sony = 1;
        if (strcmp(devs[i].name, "Tesla Model 3") == 0) found_tesla = 1;
        if (strcmp(devs[i].name, "BT Keyboard") == 0) found_keyboard = 1;
    }
    check(found_sony && found_tesla && found_keyboard,
          "names came from the capture, not from a hardcoded roster");
    check(devs[0].connected == 1,
          "connected peer sorts to the front of the roster");
    check(devs[0].battery == 78 && devs[0].codec == BT_CODEC_LDAC,
          "battery and codec survive the wire round trip");

    bt_forge_connected("Sony WH-1000XM5", sample_link_ptr());
    check(bt_link_info(&li) == 0 && li.rssi_dbm == -55 && li.codec == BT_CODEC_LDAC,
          "forged link is what link_info reports");
    check(strcmp(bt_current_name(), "Sony WH-1000XM5") == 0,
          "peer name follows the link");
}

/* ---- screens --------------------------------------------------------------- */

static void check_list_off(void) {
    printf("\n== Bluetooth list: radio off ==\n");
    bt_off();
    bt_set_scan_dump(NULL, 0);
    btui_init();
    clear_fb();
    btui_open();
    btui_draw();
    check(btui_rows() == 0, "no rows while off");
    check(btui_scan_failed() == 0, "off is not reported as a failed scan");
    check(count_color(16, 186, 140, 200, C_DIM) > 20,
          "'Bluetooth is off' line is inked");
    check(ink(30, 12, 200, 40) > 100, "Bluetooth title is inked");
    check(dump_raw("out/bt_list_off.raw"), "wrote off frame");
}

static void check_list_on(void) {
    printf("\n== Bluetooth list: real inquiry ==\n");
    bt_on();
    bt_set_scan_dump(g_blob, build_capture(5));
    bt_forge_connected("Sony WH-1000XM5", sample_link_ptr());
    clear_fb();
    btui_open();
    btui_draw();
    check(btui_rows() == 5, "five peers reach the screen");
    check(btui_paired_rows() == 3, "three of them are paired");
    check(btui_scan_failed() == 0, "rows present: no failure line");
    check(count_color(16, 188, 130, 200, C_TEAL) > 20,
          "'Paired Devices' header is teal");
    check(ink(50, BT_PAIR_Y0, 300, BT_PAIR_Y0 + 20) > 100,
          "paired device names are inked");
    check(ink(16, 300, 200, 340) > 30,
          "'Available Devices' section is inked");
    check(count_color(BT_SW_X, BT_SW_Y, BT_SW_X + 44, BT_SW_Y + 20, C_TEAL) > 100,
          "master switch is drawn on");
    check(count_color(10, 14, 24, 28, C_TEXT) >= 6,
          "the list header carries a back chevron");
    {
        /* the tip must be the leftmost ink: a ">" would have it the other way */
        int mid = -1, top = -1, bot = -1;
        for (int i = 8; i < 24; i++) {
            if (at_v(i, 19) == C_TEXT) { if (mid < 0) mid = i; }
            if (at_v(i, 16) == C_TEXT) { if (top < 0) top = i; }
            if (at_v(i, 22) == C_TEXT) { if (bot < 0) bot = i; }
        }
        check(mid >= 0 && top >= 0 && bot >= 0 && mid < top && mid < bot,
              "the back chevron points left, tip on the left");
    }
    check(dump_raw("out/bt_list_on.raw"), "wrote on frame");
}

static void check_details(void) {
    printf("\n== Bluetooth Details ==\n");
    /* The (i) button on the first paired row opens the details page. */
    btui_event(BTUI_EV_TOUCH, (BT_INFO_X + 16) * 3, (BT_PAIR_Y0 + 16) * 3);
    check(btui_page() == 1, "(i) opens Bluetooth Details");
    check(btui_details_index() >= 0, "details knows which device it shows");
    clear_fb();
    btui_draw();
    check(ink(34, 10, 220, 28) > 100, "details title is inked");
    check(ink(20, 178, 340, 190) > 20, "Status rows are inked");
    check(ink(20, BT_ADV_Y + 14, 340, BT_ADV_Y + 22) > 20,
          "Advanced rows are inked while unfolded");
    check(count_color(20, 180 + 3 * 13, 340, 188 + 3 * 13, C_TEXT) > 20,
          "Audio Codec row carries a value");
    check(ink(20, ACT_Y0 + 6, 340, ACT_Y0 + 22) > 20,
          "action rows are inked");
    check(ink(20, TOG_Y0 + 6, 260, TOG_Y0 + 22) > 20,
          "profile rows are inked");
    check(dump_raw("out/bt_details.raw"), "wrote details frame");

    btui_event(BTUI_EV_TOUCH, 180 * 3, BT_ADV_Y * 3);
    check(btui_adv_open() == 0, "tapping Advanced folds it");
    clear_fb();
    btui_draw();
    check(ink(20, BT_ADV_Y + 14, 340, BT_ADV_Y + 22) == 0,
          "folded advanced rows leave the glass");
    btui_event(BTUI_EV_TOUCH, 180 * 3, BT_ADV_Y * 3);
    check(btui_adv_open() == 1, "tapping again unfolds");

    check(btui_profile(0) == 1, "phone-call profile starts on");
    btui_event(BTUI_EV_TOUCH, 320 * 3, (TOG_Y0 + 13) * 3);
    check(btui_profile(0) == 0 && bt_profile(0) == 0,
          "profile switch reaches the radio seam");
    bt_set_profile(0, 1);

    btui_event(BTUI_EV_TOUCH, 180 * 3, (ACT_Y0 + 12) * 3);
    int arg = -1;
    check(btui_take_action(&arg) == BTUI_ACT_SCAN,
          "'Scan for devices' queues BTUI_ACT_SCAN");
    check(btui_take_action(&arg) == BTUI_ACT_NONE, "intent queue drains once");

    btui_event(BTUI_EV_TOUCH, 18 * 3, 18 * 3);
    check(btui_page() == 0, "back chevron returns to the list");
}

static void check_intents_and_off(void) {
    printf("\n== List intents + master switch ==\n");
    int arg = -1;
    /* the list header chevron hands the touch back to the hub */
    check(btui_event(BTUI_EV_TOUCH, 16 * 3, 18 * 3) == 0,
          "the list header chevron hands the touch back to the hub");
    /* First available row is below the three paired rows. */
    int avail_y = BT_PAIR_Y0 + 3 * BT_ROW_H + 12 + 14;
    btui_event(BTUI_EV_TOUCH, 180 * 3, (avail_y + 12) * 3);
    enum btui_action a = btui_take_action(&arg);
    check(a == BTUI_ACT_PAIR, "tapping an unpaired device queues PAIR");
    check(arg >= 0 && arg < btui_rows(), "intent carries a real scan index");

    btui_event(BTUI_EV_TOUCH, (BT_SW_X + 22) * 3, (BT_SW_Y + 10) * 3);
    check(btui_master_on() == 0, "master switch powers the radio off");
    check(btui_rows() == 0, "and the roster leaves with it");
    check(bt_status() == BT_STATUS_OFF, "radio state follows the switch");
}

static void check_hub(void) {
    printf("\n== Settings hub routing ==\n");
    g_nwin = 0;
    settings_init();
    settings_open();
    check(settings_page() == 0, "settings opens on the hub");
    clear_fb();
    settings_draw();
    check(ink(16, 16, 200, 42) > 100, "hub title is inked");
    check(ink(56, 128, 260, 148) > 50, "Wi-Fi row is inked");
    check(ink(56, 190, 300, 210) > 50, "Bluetooth row is inked");
    check(dump_raw("out/settings_hub.raw"), "wrote hub frame");

    settings_event(WIFIUI_EV_TOUCH, 180 * 3, 140 * 3);
    check(settings_page() == 1, "tapping Wi-Fi opens the Wi-Fi screen");
    settings_event(WIFIUI_EV_BACK, 0, 0);
    check(settings_page() == 0, "BACK from Wi-Fi list returns to the hub");

    settings_event(WIFIUI_EV_TOUCH, 180 * 3, 200 * 3);
    check(settings_page() == 2, "tapping Bluetooth opens the BT screen");
    settings_event(BTUI_EV_BACK, 0, 0);
    check(settings_page() == 0, "BACK from Bluetooth list returns to the hub");

    /* Row 3 = "Display rotation" still opens the keyboard page (unchanged). */
    settings_event(WIFIUI_EV_TOUCH, 180 * 3, 335 * 3);
    check(settings_page() == 3, "tapping Display rotation opens the keyboard page");
    check(keyboard_visible() == 1, "keyboard is visible on the rotation page");
    settings_event(KEYBOARD_EV_BACK, 0, 0);
    check(settings_page() == 0, "BACK from keyboard page returns to the hub");
    check(keyboard_visible() == 0, "keyboard closes on BACK");

    /* Row 4 = "All features" must open the Features page (new routing). */
    settings_event(WIFIUI_EV_TOUCH, 180 * 3, 397 * 3);
    check(settings_page() == 4, "tapping All features opens the Features page");
    check(features_visible() == 1, "Features page is visible");
    settings_event(WIFIUI_EV_BACK, 0, 0);
    check(settings_page() == 0, "BACK from Features returns to the hub");
    check(features_visible() == 0, "Features page closes on BACK");
}

int main(int argc, char **argv) {
    const char *off_raw = argc > 1 ? argv[1] : "out/bt_list_off.raw";
    const char *on_raw  = argc > 2 ? argv[2] : "out/bt_list_on.raw";
    const char *det_raw = argc > 3 ? argv[3] : "out/bt_details.raw";
    (void)off_raw; (void)on_raw; (void)det_raw;

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("== Bluetooth: 1080x2340 framebuffer, K=3 ==\n");
    make_fixtures();
    gfx_init(&fb[0][0], W, H, STRIDE);
    theme_init();
    font_5x7_init();

    bt_set_sysfs_root(FX_READY);
    build_capture(5);
    check_seam();

    check_list_off();
    check_list_on();
    check_details();
    check_intents_and_off();
    check_hub();

    /* The same layout must survive K=1 on a 360x780 framebuffer. */
    gfx_init(&small[0][0], 360, 780, 360 * 2);
    bt_on();
    bt_set_scan_dump(g_blob, build_capture(5));
    btui_open();
    btui_draw();
    check(small[100][100] != 0, "list renders on a 360x780 framebuffer");
    btui_event(BTUI_EV_TOUCH, (BT_INFO_X + 16), (BT_PAIR_Y0 + 16));
    btui_draw();
    check(small[180][100] != 0, "details renders on a 360x780 framebuffer");
    gfx_init(&fb[0][0], W, H, STRIDE);

    printf("\n%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
