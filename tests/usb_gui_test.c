/* SPDX-License-Identifier: MIT */
/* tests/usb_gui_test.c - "Use USB for" sheet + the real gadget mode seam.
 *
 * Nothing here pretends a DWC3 exists: usb_gadget.c is linked for real, with
 * only the FAT16/LUN helpers stubbed. What is measured is the state machine
 * the board will use (charge default, invalid mode refused, unplug returns to
 * charge) and the exact pixels of the three-choice notification on a
 * 1080x2340 RGB565 framebuffer at K=3, plus K=1 at 360x780.
 */

#include "apps/bootui/bootui.h"
#include "apps/usbui/usbui.h"
#include "gui/gfx/font_5x7.h"
#include "gui/gfx/gfx.h"
#include "gui/gfx/theme.h"
#include "hal/include/usb_gadget.h"
#include "fs/fat16.h"
#include "fs/storage.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define W 1080
#define H 2340
#define STRIDE (W * 2)

static uint16_t fb[H][W];
static uint16_t small[780][360];

static int g_checks, g_fail;

static void check(int cond, const char *label) {
    g_checks++;
    if (cond) printf("ok    %s\n", label);
    else { g_fail++; printf("FAIL  %s\n", label); }
}

/* ---- stubs for the LUN half of the gadget driver -------------------------- */

void umos_logf(const char *fmt, ...) {
    va_list ap; (void)fmt;
    va_start(ap, fmt); va_end(ap);
}

/* The command loop has its own gate (make rescue-channel-check). */
void rescue_init(struct fat16_volume *v) { (void)v; }
void rescue_poll(struct fat16_volume *v) { (void)v; }

int fat16_init(struct fat16_volume *v, uint8_t *data, size_t size) {
    (void)v; (void)data; (void)size; return 0;
}
int fat16_read_sector(struct fat16_volume *v, uint32_t lba, uint8_t *buf) {
    (void)v; (void)lba; (void)buf; return 0;
}
int fat16_write_sector(struct fat16_volume *v, uint32_t lba, const uint8_t *buf) {
    (void)v; (void)lba; (void)buf; return 0;
}
uint32_t fat16_total_sectors(struct fat16_volume *v) {
    (void)v; return 65536;
}
int storage_publish(struct fat16_volume *v) {
    (void)v; return UMOS_STORAGE_COUNT;
}

/* Capture what the phone would put on the LUN so the sync wire format is
 * measured too; the FAT16 writer itself is exercised by make storage-check. */
static char g_devinfo[1024];
static int g_devinfo_calls;
int fat16_write_file(struct fat16_volume *v, const char *name,
                     const uint8_t *data, size_t len) {
    (void)v;
    if (strcmp(name, "DEVINFO.TXT") != 0 || len >= sizeof(g_devinfo)) return -1;
    memcpy(g_devinfo, data, len);
    g_devinfo[len] = '\0';
    g_devinfo_calls++;
    return 0;
}

/* ---- measured facts ------------------------------------------------------- */

static uint16_t at_v(int vx, int vy) { return fb[vy * 3][vx * 3]; }

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

#define C_TEXT rgb565(BUI_TEXT.r, BUI_TEXT.g, BUI_TEXT.b)
#define C_TEAL rgb565(BUI_TEAL.r, BUI_TEAL.g, BUI_TEAL.b)
#define C_BG   rgb565(BUI_BG.r, BUI_BG.g, BUI_BG.b)
#define C_DIM  rgb565(BUI_DIM.r, BUI_DIM.g, BUI_DIM.b)

static int count_color(int x0, int y0, int x1, int y1, uint16_t col) {
    int n = 0;
    for (int vy = y0; vy < y1; vy++)
        for (int vx = x0; vx < x1; vx++)
            if (at_v(vx, vy) == col) n++;
    return n;
}

static int dump(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    size_t n = fwrite(&fb[0][0], 1, (size_t)W * H * 2, f);
    fclose(f);
    return n == (size_t)W * H * 2;
}

static void clear_fb(void) {
    memset(fb, 0, sizeof(fb));
    gfx_init(&fb[0][0], W, H, STRIDE);
}

static void draw_sheet(void) {
    struct umos_rect full = {0, 0, (uint16_t)gfx_width(), (uint16_t)gfx_height()};
    gfx_fill_rect(&full, BUI_BG);
    usbui_draw();
}

/* sheet geometry mirrors apps/usbui/usbui.c */
#define ROW_Y0   300
#define ROW_STEP  58
#define ROW_H     52
#define DISMISS_Y 492

/* ---- the gadget seam ------------------------------------------------------ */

static void check_seam(void) {
    printf("\n== USB gadget mode seam ==\n");
    check(usb_gadget_init() == 0, "gadget init builds the control LUN");
    check(g_devinfo_calls >= 1 && strstr(g_devinfo, "VQEAF-SYNC/1\n") != NULL,
          "the phone publishes a versioned sync record on boot");
    check(strstr(g_devinfo, "usb_mode=charge") != NULL &&
          strstr(g_devinfo, "device=violet") != NULL,
          "DEVINFO.TXT carries the real USB mode and board name");
    check(strstr(g_devinfo, "control_lun=QEAFRESCUE") != NULL,
          "the control LUN is declared so the PC tool knows the channel");
    check(strstr(g_devinfo, "battery=") == NULL &&
          strstr(g_devinfo, "serial=") == NULL,
          "no invented battery or serial is published");
    check(usb_gadget_mode() == USB_MODE_CHARGE,
          "the default mode is charge (no data endpoint)");
    check(usb_gadget_cable() == 0, "no cable at boot");
    check(strcmp(usb_gadget_mode_name(USB_MODE_MTP), "mtp") == 0,
          "mode names are the wire values the PC tool reads");
    check(usb_gadget_start() == 0, "gadget starts");
    check(usb_gadget_set_mode((enum usb_mode)99) == -1,
          "an out-of-range mode is refused");
    check(usb_gadget_set_mode(USB_MODE_MTP) == 0 &&
          usb_gadget_mode() == USB_MODE_MTP,
          "MTP can be selected");
    check(usb_gadget_set_mode(USB_MODE_PTP) == 0 &&
          usb_gadget_mode() == USB_MODE_PTP,
          "PTP can be selected");
    check(strstr(g_devinfo, "usb_mode=ptp") != NULL,
          "picking PTP republishes DEVINFO.TXT for the PC tool");
    usb_gadget_set_cable(1);
    check(usb_gadget_cable() == 1, "cable plug is observable");
    check(strstr(g_devinfo, "usb_cable=1") != NULL,
          "DEVINFO.TXT reports the cable state");
    usb_gadget_set_cable(0);
    check(usb_gadget_cable() == 0 &&
          usb_gadget_mode() == USB_MODE_CHARGE,
          "unplugging drops data mode back to the safe charge default");
    check(strstr(g_devinfo, "usb_mode=charge") != NULL &&
          strstr(g_devinfo, "usb_cable=0") != NULL,
          "unplugging republishes the safe state");
}

/* ---- the notification sheet ----------------------------------------------- */

static void check_sheet(void) {
    int arg = -1;
    printf("\n== 'Use USB for' sheet on 1080x2340 ==\n");
    usbui_init();
    check(!usbui_visible(), "the sheet is hidden until a cable is detected");
    check(usbui_row_count() == 3, "exactly three choices are offered");
    usbui_open();
    check(usbui_visible(), "plugging the cable opens the sheet");
    check(usbui_take_action(&arg) == USBUI_ACT_NONE,
          "opening queues nothing: no mode is silently preselected");

    clear_fb();
    draw_sheet();
    check(count_color(28, 220, 300, 244, C_TEXT) > 60,
          "the 'Use USB for' title is inked");
    for (int i = 0; i < 3; i++) {
        char lab[48];
        snprintf(lab, sizeof(lab), "choice row %d is inked", i);
        check(count_color(70, ROW_Y0 + i * ROW_STEP + 8, 300,
                          ROW_Y0 + i * ROW_STEP + 40, C_TEXT) > 20, lab);
    }
    check(count_color(28, ROW_Y0, 332, ROW_Y0 + ROW_H, C_TEAL) > 8,
          "the current (charge) row is marked teal");
    check(count_color(344, 550, 360, 570, C_DIM) == 0,
          "the footer copy stays inside the sheet, not clipped");
    check(dump("out/usb_sheet_charge.raw"), "wrote charge-sheet RGB565 dump");

    /* each row queues exactly its own mode, with no write performed here */
    for (int i = 0; i < 3; i++) {
        int rc = usbui_event(USBUI_EV_TOUCH, 180 * 3,
                             (ROW_Y0 + i * ROW_STEP + 26) * 3);
        char lab[64];
        snprintf(lab, sizeof(lab), "row %d queues SET_MODE %d", i,
                 USB_MODE_CHARGE + i);
        check(rc == 1 &&
              usbui_take_action(&arg) == USBUI_ACT_SET_MODE &&
              arg == USB_MODE_CHARGE + i, lab);
    }
    check(usbui_take_action(&arg) == USBUI_ACT_NONE,
          "the intent queue drains once");

    check(usbui_event(USBUI_EV_BACK, 0, 0) == 1 &&
          usbui_take_action(&arg) == USBUI_ACT_DISMISS,
          "BACK dismisses without changing mode");
    usbui_event(USBUI_EV_TOUCH, 180 * 3, (DISMISS_Y + 10) * 3);
    check(usbui_take_action(&arg) == USBUI_ACT_DISMISS,
          "'Not now' dismisses without changing mode");

    /* the teal marker must follow the live mode, not a stale copy */
    usb_gadget_set_mode(USB_MODE_PTP);
    clear_fb();
    draw_sheet();
    check(count_color(28, ROW_Y0 + 2 * ROW_STEP, 332,
                      ROW_Y0 + 2 * ROW_STEP + ROW_H, C_TEAL) > 8,
          "the teal marker moves to the PTP row");
    check(dump("out/usb_sheet_ptp.raw"), "wrote ptp-sheet RGB565 dump");
}

int main(void) {
    printf("== USB mode notification: real framebuffer, K=3 ==\n");
    gfx_init(&fb[0][0], W, H, STRIDE);
    theme_init();
    font_5x7_init();

    check_seam();
    check_sheet();

    /* same sheet must survive K=1 on a 360x780 panel */
    gfx_init(&small[0][0], 360, 780, 360 * 2);
    usbui_open();
    usbui_draw();
    check(small[350][100] != 0, "the sheet renders on a 360x780 framebuffer");

    printf("\n%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
