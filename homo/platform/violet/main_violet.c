/* SPDX-License-Identifier: MIT */
/* platform/violet/main_violet.c - Homo OS PID 1 on the Redmi Note 7 Pro.
 *
 * One process owns everything: mounts, /dev, the QEAFRESCUE LUN, the DRM
 * scanout, touch and the LVGL desktop. There is no init system, no shell and
 * no second binary in the initramfs; if the panel cannot be brought up the
 * process keeps the rescue drive alive instead of panicking the kernel.
 */
#define _GNU_SOURCE

#include "lvgl.h"

#include "framework/desktop.h"

#include "drm_kms.h"
#include "panel_scale.h"
#include "usb_lun.h"
#include "vio_display.h"
#include "violet.h"
#include "violet_input.h"
#include "violet_power.h"

#include "fs/rescue.h"
#include "qeaf_version.h"
#include "core/i18n.h"
#include "core/prefs.h"
#include "framework/ui/i18n.h"
#include "hal/include/display.h"
#include "services/music.h"

#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/reboot.h>
#include <linux/reboot.h>

/* The portrait shell (540x1170, a 2x fit of the 1080x2340 panel of the
 * panel) is the larger of the two canvases, so it sizes the buffer the LVGL
 * display renders into. Landscape is 1170x540. App modules run on the
 * 800x480 canvas, which fits inside it. */
static uint16_t ui_buffer[VIO_UI_PORTRAIT_W * VIO_UI_PORTRAIT_H];
static lv_display_t *ui_display;
static int ui_w = VIO_UI_PORTRAIT_W;
static int ui_h = VIO_UI_PORTRAIT_H;
/* What the canvas above was built for, so a rescue `rotate`, the auto-rotate
 * sensor or an app launch is noticed once instead of every frame. */
static int ui_orientation;
static int ui_app_canvas;
static vio_panel_t panel;
static struct vio_drm drm;
static struct vio_touch touch;
static struct vio_power power_btn;
static struct vio_lun lun;
static lv_obj_t *power_menu = NULL;
static lv_obj_t *power_menu_ctx = NULL;
static int drm_ready;
static volatile sig_atomic_t ui_ready;
/* Orientation to persist: with auto-rotate on, the sensor moves the panel and
 * a transient tilt is not a preference. Loaded once before the panel binds. */
static int pref_rot = DISPLAY_LANDSCAPE;
static int prefs_ready;

/* The rescue channel's `snap` verb reads the live scanout buffer, not the
 * LVGL source buffer: the host must see exactly what the panel is being fed,
 * including the scale, the rotation and the letterbox. The geometry and byte
 * count live in fs/rescue.h so usb_lun.c can reserve the whole cluster chain
 * before the host mounts the volume: allocating 61 clusters later would
 * collide with files Windows created in the meantime. */
static uint16_t snap_pixels[RESCUE_SCREEN_W * RESCUE_SCREEN_H];

static int snapshot_scanout(struct fat16_volume *vol)
{
    int x, y;

    if(!drm_ready || drm.map == NULL) return -1;
    for(y = 0; y < RESCUE_SCREEN_H; y++) {
        int sy = (int)(((int64_t)y * drm.height) / RESCUE_SCREEN_H);
        const uint32_t *row = (const uint32_t *)(drm.map +
                                                 (size_t)sy * drm.pitch);
        for(x = 0; x < RESCUE_SCREEN_W; x++) {
            int sx = (int)(((int64_t)x * drm.width) / RESCUE_SCREEN_W);
            uint32_t p = row[sx];
            uint16_t r = (uint16_t)((p >> 19) & 0x1F);
            uint16_t g = (uint16_t)((p >> 10) & 0x3F);
            uint16_t b = (uint16_t)((p >> 3) & 0x1F);
            snap_pixels[y * RESCUE_SCREEN_W + x] =
                (uint16_t)((r << 11) | (g << 5) | b);
        }
    }
    if(fat16_write_file(vol, "SCREEN.RAW", (const uint8_t *)snap_pixels,
                        sizeof(snap_pixels)) < 0)
        return -2;
    vio_log("snap: wrote SCREEN.RAW (%dx%d RGB565, %u bytes)",
            RESCUE_SCREEN_W, RESCUE_SCREEN_H, (unsigned)sizeof(snap_pixels));
    return (int)sizeof(snap_pixels);
}

/* The rescue drive is the only way back into a phone whose panel path is still
 * unproven, so a fault inside LVGL must not take PID 1 down with it (an exiting
 * PID 1 panics the kernel and the USB gadget disappears). Faults outside the UI
 * are real bugs and are re-raised. */
static sigjmp_buf ui_recovery;
static volatile sig_atomic_t in_ui;

static void ui_fault(int sig)
{
    if(in_ui) {
        static const char message[] =
            "<2>homo: UI fault, rescue drive stays up\n";
        ssize_t ignored = write(STDERR_FILENO, message, sizeof(message) - 1);
        (void)ignored;
        in_ui = 0;
        ui_ready = 0;
        siglongjmp(ui_recovery, 1);
    }
    signal(sig, SIG_DFL);
    raise(sig);
}

/* The durable copy is the CRC-checked record in the tail of p77; /etc/qeaf.conf
 * is the session copy. The slot wins because it is the one the next boot reads.
 * This runs after devnodes.c has created /dev/mmcblk0p77 and before the panel
 * is bound, so display_set_orientation() is applied by vio_display_bind(). */
static void load_session_prefs(void)
{
    int lang = (int)I18N_VI; /* Vietnamese portrait is the shipped default; a stored value wins */
    int rot = (int)display_orientation();
    int auto_rotate = display_auto_rotate();

    prefs_set_block(PREFS_P77_PATH, PREFS_P77_SLOT_OFFSET);
    if(prefs_load(&lang, &rot, &auto_rotate) == 0) {
        if(lang >= 0 && lang < I18N_LANG_COUNT) i18n_set((enum i18n_lang)lang);
        if(rot >= 0 && rot < DISPLAY_ORIENTATION_COUNT)
            display_set_orientation((enum display_orientation)rot);
        display_set_auto_rotate(auto_rotate ? 1 : 0);
        vio_log("prefs: language %d, orientation %s, auto %d (%s)",
                (int)i18n_selected(), display_orientation_name(display_orientation()),
                display_auto_rotate(), prefs_source());
    }
    else {
        vio_log("prefs: no readable %s; using defaults", PREFS_DEFAULT_PATH);
    }
    /* Keep core/i18n and the label catalog in step even when no record was
     * found; otherwise the first prefs_sync would write the EN core default
     * over the Chinese desktop default. */
    if(lang >= 0 && lang < I18N_LANG_COUNT) i18n_set((enum i18n_lang)lang);
    homo_i18n_set(lang);
    homo_i18n_attach();
    pref_rot = (int)display_orientation();
    prefs_ready = 1;
}

/* Only a real change writes: prefs_sync() compares against the last load/save.
 * A rescue `rotate` and any future UI both land here. */
static void save_session_prefs(void)
{
    if(!prefs_ready) return;
    if(!display_auto_rotate()) pref_rot = (int)display_orientation();
    if(prefs_sync((int)i18n_selected(), pref_rot, display_auto_rotate()) == 1) {
        vio_log("prefs: saved language %d, orientation %d, auto %d (%s)",
                (int)i18n_selected(), pref_rot, display_auto_rotate(),
                prefs_source());
    }
}

/* Language switch from the rescue channel (QEAF.CMD `lang`). core/i18n is the
 * value prefs_sync() stores; homo_i18n drives the labels. Existing labels keep
 * their text, so the desktop is rebuilt exactly like the Settings page does. */
static int apply_rescue_language(int lang)
{
    if(lang < 0 || lang >= I18N_LANG_COUNT) return -1;
    i18n_set((enum i18n_lang)lang);
    homo_i18n_set(lang);
    if(ui_ready) {
        hachimi_desktop_destroy();
        ios_desktop_create();
    }
    vio_log("lang: rescue selected %d (%s)", lang, homo_i18n_lang_name(lang));
    return 0;
}

static uint32_t tick_cb(void)
{
    return (uint32_t)vio_monotonic_ms();
}

static void flush_cb(lv_display_t *display, const lv_area_t *area, uint8_t *px_map)
{
    (void)px_map; /* DIRECT mode renders into ui_buffer itself. */

    if(drm_ready && drm.map) {
        if(vio_display_take_redraw()) {
            vio_panel_render(&panel, ui_buffer, (uint32_t *)drm.map,
                             vio_panel_full_rect(&panel));
            vio_drm_flush(&drm, 0, 0, drm.width - 1, drm.height - 1);
        }
        else {
            vio_rect_t ui;
            vio_rect_t out;
            ui.x1 = area->x1;
            ui.y1 = area->y1;
            ui.x2 = area->x2;
            ui.y2 = area->y2;
            out = vio_panel_map_ui_rect(&panel, ui);
            if(out.x1 <= out.x2 && out.y1 <= out.y2) {
                vio_panel_render(&panel, ui_buffer, (uint32_t *)drm.map, out);
                vio_drm_flush(&drm, out.x1, out.y1, out.x2, out.y2);
            }
        }
    }
    lv_display_flush_ready(display);
}

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    vio_touch_poll(&touch);
    data->point.x = (int32_t)touch.x;
    data->point.y = (int32_t)touch.y;
    data->state = touch.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    data->continue_reading = 0;
}

static void power_menu_close(void) {
    if (power_menu) { lv_obj_del(power_menu); power_menu = NULL; }
}

static lv_obj_t *volume_hud = NULL;
static lv_obj_t *volume_bar = NULL;
static lv_obj_t *volume_label = NULL;
static uint64_t volume_hud_until = 0;

static void volume_hud_show(int volume) {
    if (!volume_hud) {
        lv_obj_t *scr = lv_screen_active();
        volume_hud = lv_obj_create(scr);
        lv_obj_set_style_bg_color(volume_hud, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(volume_hud, LV_OPA_80, LV_PART_MAIN);
        lv_obj_set_size(volume_hud, 320, 120);
        lv_obj_align(volume_hud, LV_ALIGN_CENTER, 0, -100);
        volume_label = lv_label_create(volume_hud);
        lv_obj_set_style_text_color(volume_label, lv_color_white(), LV_PART_MAIN);
        lv_obj_align(volume_label, LV_ALIGN_TOP_MID, 0, 10);
        volume_bar = lv_bar_create(volume_hud);
        lv_obj_set_size(volume_bar, 280, 24);
        lv_obj_align(volume_bar, LV_ALIGN_BOTTOM_MID, 0, -10);
        lv_bar_set_range(volume_bar, 0, 100);
        lv_bar_set_value(volume_bar, volume, LV_ANIM_OFF);
        char _vb[24]; snprintf(_vb, sizeof(_vb), "Volume %d", volume); lv_label_set_text(volume_label, _vb);
    } else {
        lv_bar_set_value(volume_bar, volume, LV_ANIM_OFF);
        char _vb[24]; snprintf(_vb, sizeof(_vb), "Volume %d", volume); lv_label_set_text(volume_label, _vb);
    }
    lv_obj_remove_flag(volume_hud, LV_OBJ_FLAG_HIDDEN);
    volume_hud_until = vio_monotonic_ms() + 2000;
}

static void power_action_cb(lv_event_t *e) {
    void *u = lv_event_get_user_data(e);
    int act = u ? (int)(intptr_t)u : 0;
    power_menu_close();
    if (act == 1) {
        vio_log("power: menu -> power off");
        sync();
        reboot(RB_POWER_OFF);
    } else if (act == 2) {
        vio_log("power: menu -> reboot");
        sync();
        reboot(RB_AUTOBOOT);
    } else if (act == 3) {
        vio_log("power: menu -> fastboot");
        sync();
        reboot(RB_AUTOBOOT); /* fastboot via reboot to bootloader */
    }
}

static void power_menu_show(void) {
    if (power_menu) return;
    lv_obj_t *scr = lv_screen_active();
    power_menu = lv_obj_create(scr);
    lv_obj_set_style_bg_color(power_menu, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(power_menu, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_size(power_menu, 320, 280);
    lv_obj_align(power_menu, LV_ALIGN_CENTER, 0, 0);
    lv_obj_t *title = lv_label_create(power_menu);
    lv_label_set_text(title, "Power");
    lv_obj_set_style_text_color(title, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);
    const char *names[3] = {"Power off", "Reboot", "Fastboot"};
    for (int i = 0; i < 3; i++) {
        lv_obj_t *btn = lv_btn_create(power_menu);
        lv_obj_set_size(btn, 260, 50);
        lv_obj_align(btn, LV_ALIGN_CENTER, 0, -40 + i * 70);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, names[i]);
        lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
        lv_obj_add_event_cb(btn, power_action_cb, LV_EVENT_CLICKED, (void *)(intptr_t)(i + 1));
    }
}

static int start_ui(void)
{
    lv_indev_t *indev;
    char touch_path[320];
    const char *found;

    lv_init();
    lv_tick_set_cb(tick_cb);

    vio_ui_canvas_size(display_orientation(), &ui_w, &ui_h);
    ui_orientation = (int)display_orientation();
    ui_app_canvas = 0;

    ui_display = lv_display_create(ui_w, ui_h);
    if(ui_display == NULL) {
        vio_log("ui: lv_display_create failed");
        return -1;
    }
    lv_display_set_color_format(ui_display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(ui_display, ui_buffer, NULL, (uint32_t)sizeof(ui_buffer),
                           LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(ui_display, flush_cb);
    lv_display_set_default(ui_display);
    vio_display_bind(&panel);

    /* Splash: white text on black, terminal typing effect.
     * Runs ~2.5s before desktop. LVGL timers + vio_sleep_ms drive it. */
    {
        lv_obj_t *scr = lv_screen_active();
        lv_obj_set_style_bg_color(scr, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_pad_all(scr, 0, LV_PART_MAIN);
        {
            lv_obj_t *logo = lv_label_create(scr);
            lv_label_set_text(logo, "VQEAF OS");
            lv_obj_set_style_text_color(logo, lv_color_white(), LV_PART_MAIN);
            lv_obj_set_style_text_font(logo, &lv_font_montserrat_32, LV_PART_MAIN);
            lv_obj_align(logo, LV_ALIGN_CENTER, 0, -60);
            lv_obj_t *term = lv_label_create(scr);
            lv_obj_set_style_text_color(term, lv_color_white(), LV_PART_MAIN);
            lv_obj_set_style_text_font(term, &lv_font_montserrat_12, LV_PART_MAIN);
            lv_obj_align(term, LV_ALIGN_CENTER, 0, 20);
            const char *lines[] = {
                "VQEAF OS 0.1.0 starting...",
                "drm: scanout ready",
                "lun: QEAFRESCUE bound",
                "ui: desktop 540x1170",
            };
            const int total = 4;
            static char buf[256];
            int shown = 0;
            uint32_t t0 = vio_monotonic_ms();
            buf[0] = 0;
            while (shown <= total) {
                uint32_t el = vio_monotonic_ms() - t0;
                /* advance every 600ms */
                int want = (int)(el / 600) + 1;
                if (want > total + 1) want = total + 1;
                if (want != shown) {
                    buf[0] = 0;
                    for (int i = 0; i < want - 1 && i < total; i++) {
                        strcat(buf, lines[i]);
                        strcat(buf, "\n");
                    }
                    if (want <= total) strcat(buf, lines[want - 1]);
                    /* blinking cursor */
                    if (((int)(el / 300) % 2) == 0 && buf[0]) {
                        size_t n = strlen(buf);
                        if (n + 2 < sizeof(buf)) { buf[n] = '_'; buf[n + 1] = 0; }
                    }
                    lv_label_set_text(term, buf);
                    shown = want;
                    lv_refr_now(NULL);
                }
                lv_timer_handler();
                vio_sleep_ms(50);
                if (el > 3000) break;
            }
            lv_obj_del(logo);
            lv_obj_del(term);
        }
    }

    found = vio_find_touch_device(touch_path, sizeof(touch_path));
    if(found != NULL && vio_touch_open(&touch, found, &panel) == 0) {
        indev = lv_indev_create();
        if(indev != NULL) {
            lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
            lv_indev_set_read_cb(indev, touch_read_cb);
            lv_indev_set_display(indev, ui_display);
        }
    }
    else {
        vio_log("ui: no touch device; desktop is display-only");
    }

    /* Power + Volume buttons: scan /dev/input for KEY_POWER / KEY_VOLUME* */
    {
        DIR *d = opendir("/dev/input");
        if (d) {
            struct dirent *e;
            while ((e = readdir(d)) != NULL) {
                if (strncmp(e->d_name, "event", 5) != 0) continue;
                char pp[320];
                snprintf(pp, sizeof(pp), "/dev/input/%s", e->d_name);
                int fd = open(pp, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
                if (fd < 0) continue;
                unsigned char bits[KEY_MAX / 8 + 1];
                memset(bits, 0, sizeof(bits));
                if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(bits)), bits) >= 0 &&
                    (((bits[KEY_POWER / 8] >> (KEY_POWER % 8)) & 1) ||
                     ((bits[KEY_VOLUMEUP / 8] >> (KEY_VOLUMEUP % 8)) & 1) ||
                     ((bits[KEY_VOLUMEDOWN / 8] >> (KEY_VOLUMEDOWN % 8)) & 1))) {
                    close(fd);
                    if (power_btn.nfd == 0) {
                        if (vio_power_open(&power_btn, pp) == 0)
                            vio_log("power: using %s", pp);
                    } else {
                        if (vio_power_open_more(&power_btn, pp) == 0)
                            vio_log("power: using %s (more)", pp);
                    }
                } else {
                    close(fd);
                }
            }
            closedir(d);
        }
        if (power_btn.nfd == 0) vio_log("power: no KEY_POWER/VOLUME node found");
    }

    ios_desktop_create();
    vio_log("ui: " QEAF_OS_NAME " desktop created at %dx%d", ui_w, ui_h);
    return 0;
}

/* The shell follows the panel orientation and app modules get the 800x480
 * canvas they were drawn for; both share the one display, so a change is a
 * resolution update plus a relayout, not a new desktop. Landscape apps
 * (non-Settings) force the effective landscape rotation while open, so they
 * follow the landscape screen; closing restores the shell orientation. */
static void ui_sync_canvas(void)
{
    int w, h, orientation, app;

    if(!ui_ready || ui_display == NULL) return;
    orientation = (int)display_orientation();
    app = hachimi_desktop_app_mode() ? 1 : 0;
    int effective = app ? DISPLAY_LANDSCAPE : orientation;
    if(effective == ui_orientation && app == ui_app_canvas) return;

    if(app) {
        w = VIO_UI_W;
        h = VIO_UI_H;
    }
    else {
        vio_ui_canvas_size((enum display_orientation)orientation, &w, &h);
    }
    ui_orientation = effective;
    ui_app_canvas = app;

    if(w != ui_w || h != ui_h) {
        /* Resolution and buffer must move together: the draw buffer's stride
         * is derived from the canvas width in lv_display_set_buffers(). */
        lv_display_set_resolution(ui_display, w, h);
        lv_display_set_buffers(ui_display, ui_buffer, NULL,
                               (uint32_t)sizeof(ui_buffer),
                               LV_DISPLAY_RENDER_MODE_DIRECT);
        ui_w = w;
        ui_h = h;
        vio_panel_set_ui_size(&panel, w, h);
    }
    vio_display_bind(&panel);
    vio_panel_set_rotation(&panel, display_orientation_degrees((enum display_orientation)effective));
    hachimi_desktop_relayout();
    vio_log("ui: canvas %dx%d (%s%s)", w, h,
            display_orientation_name((enum display_orientation)effective),
            app ? ", app" : "");
}

static void install_ui_fault_handlers(void)
{
    static const int faults[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT };
    struct sigaction action;
    size_t i;

    memset(&action, 0, sizeof(action));
    action.sa_handler = ui_fault;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    for(i = 0; i < sizeof(faults) / sizeof(faults[0]); i++)
        sigaction(faults[i], &action, NULL);
}

int main(void)
{
    uint64_t last_poll;
    uint64_t last_gadget_retry = 0;

    signal(SIGPIPE, SIG_IGN);
    install_ui_fault_handlers();
    touch.fd = -1;
    drm.fd = -1;
    lun.fd = -1;

    vio_setup_environment();
    load_session_prefs();
    vio_log("boot: " QEAF_OS_NAME " init on violet (PID %d)", (int)getpid());
    rescue_set_snapshot_hook(snapshot_scanout);
    rescue_set_language_hook(apply_rescue_language);

    /* The rescue drive is the only way back into a phone whose panel path is
     * unproven, so bind it before anything that can block or fault: the DRM
     * probe wait, the backlight and the desktop all come after the host can
     * already see QEAFRESCUE. */
    if(sigsetjmp(ui_recovery, 1) == 0) {
        in_ui = 1;
        if(vio_lun_init(&lun) == 0) {
            if(vio_lun_start_gadget(&lun) < 0)
                vio_log("boot: LUN built but USB gadget is not bound; will retry");
        }
        else {
            vio_log("boot: QEAFRESCUE volume unavailable");
        }
        in_ui = 0;
    }
    else {
        vio_log("boot: LUN init faulted; continuing");
    }

    /* The panel probes asynchronously on some DT revisions; give the DRM
     * device a moment before deciding the display is unavailable. */
    if(vio_wait_for_path("/dev/dri/card0", 8000) < 0)
        vio_log("boot: /dev/dri/card0 did not appear; continuing without UI");

    if(sigsetjmp(ui_recovery, 1) == 0) {
        in_ui = 1;
        vio_set_backlight(85);
        in_ui = 0;
    }
    else {
        in_ui = 0;
        vio_log("boot: backlight set faulted");
    }

    if(sigsetjmp(ui_recovery, 1) == 0) {
        in_ui = 1;
        vio_ui_canvas_size(display_orientation(), &ui_w, &ui_h);
        vio_panel_init(&panel, ui_w, ui_h, VIO_PANEL_W, VIO_PANEL_H,
                       display_orientation_degrees(display_orientation()));
        if(vio_drm_init(&drm, VIO_PANEL_W, VIO_PANEL_H) == 0) {
            drm_ready = 1;
            /* The dumb buffer's pitch is 4352 bytes for 1080 pixels, i.e.
             * 1088 pixels per row. Rendering with panel_w as the stride
             * sheared every row by 8 pixels on the glass. */
            vio_panel_set_stride(&panel, (int)(drm.pitch / 4));
            in_ui = 0;
            if(sigsetjmp(ui_recovery, 1) == 0) {
                in_ui = 1;
                if(start_ui() == 0) ui_ready = 1;
                in_ui = 0;
            }
            else {
                vio_log("boot: desktop faulted during start; LUN stays up");
            }
        }
        else {
            in_ui = 0;
            vio_log("boot: DRM scanout unavailable; UI disabled, LUN stays up");
        }
    }
    else {
        in_ui = 0;
        vio_log("boot: panel/DRM init faulted; UI disabled, LUN stays up");
    }

    last_poll = vio_monotonic_ms();
    for(;;) {
        if(ui_ready) {
            /* Re-arm the recovery point every frame: a fault inside LVGL drops
             * the desktop but never the rescue drive. */
            if(sigsetjmp(ui_recovery, 1) == 0) {
                uint32_t idle;

                in_ui = 1;
                idle = lv_timer_handler();
                /* After the frame, so a tap that asked for the app canvas is
                 * served before the next refresh. */
                ui_sync_canvas();
                if (vio_power_poll(&power_btn)) {
                    power_menu_show();
                }
                if (power_btn.volume_delta != 0) {
                    hachimi_music_volume((unsigned)power_btn.volume);
                    volume_hud_show(power_btn.volume);
                }
                if (volume_hud && vio_monotonic_ms() >= volume_hud_until) {
                    lv_obj_add_flag(volume_hud, LV_OBJ_FLAG_HIDDEN);
                }
                in_ui = 0;

                if(idle == 0) idle = 1;
                if(idle > 16) idle = 16;
                vio_sleep_ms(idle);
            }
            else {
                vio_log("boot: desktop faulted; UI stopped, LUN stays up");
            }
        }
        else {
            vio_sleep_ms(16);
        }

        if(lun.ready && vio_monotonic_ms() - last_poll >= 250) {
            uint64_t now = vio_monotonic_ms();
            /* The dwc3 UDC can register after the first bind attempt; keep
             * trying so a late probe still gives the host a rescue drive. */
            if(!lun.gadget && now - last_gadget_retry >= 3000) {
                last_gadget_retry = now;
                if(sigsetjmp(ui_recovery, 1) == 0) {
                    in_ui = 1;
                    if(vio_lun_start_gadget(&lun) == 0)
                        vio_log("boot: USB gadget bound on retry");
                    in_ui = 0;
                }
                else {
                    in_ui = 0;
                    vio_log("boot: gadget retry faulted");
                }
            }
            if(sigsetjmp(ui_recovery, 1) == 0) {
                in_ui = 1;
                rescue_poll(&lun.volume);
                vio_lun_poll(&lun);
                display_poll();
                if (vio_power_poll(&power_btn)) {
                    power_menu_show();
                }
                if (power_btn.volume_delta != 0) {
                    hachimi_music_volume((unsigned)power_btn.volume);
                    volume_hud_show(power_btn.volume);
                }
                if (volume_hud && vio_monotonic_ms() >= volume_hud_until) {
                    lv_obj_add_flag(volume_hud, LV_OBJ_FLAG_HIDDEN);
                }
                in_ui = 0;
            }
            else {
                in_ui = 0;
                vio_log("boot: rescue/display poll faulted; LUN stays up");
            }
            last_poll = vio_monotonic_ms();
        }
        save_session_prefs();
    }
}
