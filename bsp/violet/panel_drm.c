/* SPDX-License-Identifier: MIT */
/* bsp/violet/panel_drm.c - the panel, over DRM/KMS, from PID 1.
 *
 * Why not fbdev: the violet kernel is built with CONFIG_DRM_FBDEV_EMULATION
 * unset, so /dev/fb0, /dev/graphics/fb0 and /sys/class/graphics/fb0 do not
 * exist while /dev/dri/card0 does (measured in the old tree's
 * out/tmp/oldsrc/init/panel_drm.c, which this file ports). The SDE driver also
 * reports fb_id 0 on a panel that is demonstrably powered, so "the kernel is
 * already scanning something" is not true either: a client has to allocate a
 * buffer, register it and commit it to the CRTC.
 *
 * The commit uses the timings the kernel itself reported - the CRTC's mode, or
 * the connector's first mode - and never invents a lane count, pixel clock or
 * porch. On a DSI bus an invented mode can look like a dead phone, so a mode
 * the kernel did not offer is a refusal here, not a guess.
 *
 * One master: DRM allows one. With `qeaf.umos` on the command line the GUI
 * shell paints, and the old init was told to stand down by `qeaf.nopanel`. This
 * tree has one process - qeafinit holds the card, modesets once and hands the
 * mapped buffer to gui/gfx - so the flag has nothing to arbitrate any more.
 *
 * Failure policy: a panel that will not come up logs one sentence and returns
 * SUCCESS, because hal_init() treats a non-zero return as "the board is
 * unusable" and would kill PID 1 before the USB gadget is ever bound. A dark
 * screen with a reachable LUN is recoverable; a dead PID 1 is not.
 */

#include "hal/include/hal.h"
#include "core/sysio.h"
#include "core/log.h"
#include <stdint.h>

#define DRM_DEV       "/dev/dri/card0"
#define DRM_DEV_DIR   "/dev/dri"
#define DRM_SYSFS_DEV "/sys/class/drm/card0/dev"
#define DRM_CARD_MAJOR 226u          /* misc device major the kernel uses */
#define DRM_IDS       16             /* bounded: a rescue path must not allocate */

#define O_RDWR_ 2
#define S_IFCHR_ 0020000u

#define MKDEV(ma, mi) (((unsigned)(ma) << 8) | ((unsigned)(mi) & 0xffu) | \
                       (((unsigned)(mi) & ~0xffu) << 12))

/* ---------------------------------------------------------------------------
 * The uapi this file needs, spelled out because the device link has no libc and
 * so no <drm/drm_mode.h>. Field order and widths are the kernel's; the ioctl
 * numbers below are derived from these sizeof()s exactly the way _IOWR does it,
 * so a struct that drifts changes the request number with it.
 */
struct drm_mode_modeinfo {
    uint32_t clock;
    uint16_t hdisplay, hsync_start, hsync_end, htotal, hskew;
    uint16_t vdisplay, vsync_start, vsync_end, vtotal, vscan;
    uint32_t vrefresh;
    uint32_t flags;
    uint32_t type;
    char name[32];
};

struct drm_mode_get_resources {
    uint64_t fb_id_ptr;
    uint64_t crtc_id_ptr;
    uint64_t connector_id_ptr;
    uint64_t encoder_id_ptr;
    uint32_t count_fbs;
    uint32_t count_crtcs;
    uint32_t count_connectors;
    uint32_t count_encoders;
    uint32_t min_width, max_width, min_height, max_height;
};

struct drm_mode_get_connector {
    uint64_t encoders_ptr;
    uint64_t modes_ptr;
    uint64_t props_ptr;
    uint64_t prop_values_ptr;
    uint32_t count_modes;
    uint32_t count_props;
    uint32_t count_encoders;
    uint32_t encoder_id;
    uint32_t connector_id;
    uint32_t connector_type;
    uint32_t connector_type_id;
    uint32_t connection;
    uint32_t mm_width, mm_height;
    uint32_t subpixel;
    uint32_t pad;
};

struct drm_mode_get_encoder {
    uint32_t encoder_id;
    uint32_t encoder_type;
    uint32_t crtc_id;
    uint32_t possible_crtcs;
    uint32_t possible_clones;
};

struct drm_mode_crtc {
    uint64_t set_connectors_ptr;
    uint32_t count_connectors;
    uint32_t crtc_id;
    uint32_t fb_id;
    uint32_t x, y;
    uint32_t gamma_size;
    uint32_t mode_valid;
    struct drm_mode_modeinfo mode;
};

struct drm_mode_create_dumb {
    uint32_t height, width, bpp, flags;
    uint32_t handle, pitch;
    uint64_t size;
};

struct drm_mode_map_dumb {
    uint32_t handle;
    uint32_t pad;
    uint64_t offset;
};

struct drm_mode_fb_cmd {
    uint32_t fb_id, width, height, pitch, bpp, depth, handle;
};

#define DRM_IO(nr)      (0x00006400ul | (unsigned long)(nr))
#define DRM_IOWR(nr, t) (0xc0000000ul | ((unsigned long)(sizeof(t) & 0x3fffu) << 16) | \
                         0x6400ul | (unsigned long)(nr))

#define DRM_IOCTL_SET_MASTER        DRM_IO(0x1F)
#define DRM_IOCTL_MODE_GETRESOURCES DRM_IOWR(0xA0, struct drm_mode_get_resources)
#define DRM_IOCTL_MODE_GETCRTC      DRM_IOWR(0xA1, struct drm_mode_crtc)
#define DRM_IOCTL_MODE_SETCRTC      DRM_IOWR(0xA2, struct drm_mode_crtc)
#define DRM_IOCTL_MODE_GETENCODER   DRM_IOWR(0xA6, struct drm_mode_get_encoder)
#define DRM_IOCTL_MODE_GETCONNECTOR DRM_IOWR(0xA7, struct drm_mode_get_connector)
#define DRM_IOCTL_MODE_ADDFB        DRM_IOWR(0xAE, struct drm_mode_fb_cmd)
#define DRM_IOCTL_MODE_CREATE_DUMB  DRM_IOWR(0xB2, struct drm_mode_create_dumb)
#define DRM_IOCTL_MODE_MAP_DUMB     DRM_IOWR(0xB3, struct drm_mode_map_dumb)

#define DRM_MODE_CONNECTED 1u

static int g_fd = -1;
static uint8_t *g_map;
static uint32_t g_w, g_h, g_pitch;   /* g_pitch is BYTES: gfx divides by two */
static uint32_t g_fb_id;
static char g_why[160];

static void z(void *p, unsigned n) {
    uint8_t *b = (uint8_t *)p;
    while (n--) *b++ = 0;
}

static void refuse(const char *why) {
    if (g_why[0]) return;               /* the first reason is the readable one */
    unsigned i = 0;
    while (why[i] && i + 1 < sizeof(g_why)) { g_why[i] = why[i]; i++; }
    g_why[i] = '\0';
    umos_logf("panel: %s\n", g_why);
}

/* # CONFIG_DEVTMPFS is not set, so the node is made here from what the kernel
 * published in sysfs. Without this the card exists and nothing can open it. */
static int ensure_node(void) {
    char buf[32];
    unsigned maj = 0, min = 0, i = 0;

    if (sysio_exists(DRM_DEV)) return 0;
    if (sysio_read_file(DRM_SYSFS_DEV, buf, (int)sizeof(buf)) > 0) {
        while (buf[i] >= '0' && buf[i] <= '9') { maj = maj * 10u + (unsigned)(buf[i] - '0'); i++; }
        if (maj && buf[i] == ':') {
            i++;
            while (buf[i] >= '0' && buf[i] <= '9') { min = min * 10u + (unsigned)(buf[i] - '0'); i++; }
        } else {
            maj = 0;
        }
    }
    if (!maj) { maj = DRM_CARD_MAJOR; min = 0; }
    sysio_mkdir_p(DRM_DEV_DIR);
    if (sysio_mknod(DRM_DEV, S_IFCHR_ | 0600u, MKDEV(maj, min)) != 0) {
        refuse("cannot create /dev/dri/card0 - the panel is unreachable");
        return -1;
    }
    umos_logf("panel: made %s (%u:%u)\n", DRM_DEV, maj, min);
    return 0;
}

/* Timings: the CRTC's own mode first, then the connected connector's first
 * mode. Anything else would be a mode this kernel never offered. */
static int find_mode(uint32_t connector_id, uint32_t crtc_id,
                     struct drm_mode_modeinfo *out) {
    struct drm_mode_crtc crtc;
    struct drm_mode_get_connector con;
    struct drm_mode_modeinfo modes[DRM_IDS];
    uint32_t con_id = connector_id;

    z(&crtc, sizeof(crtc));
    crtc.crtc_id = crtc_id;
    if (sysio_ioctl(g_fd, DRM_IOCTL_MODE_GETCRTC, &crtc) == 0 &&
        crtc.mode_valid && crtc.mode.hdisplay && crtc.mode.vdisplay) {
        *out = crtc.mode;
        return 0;
    }

    z(&con, sizeof(con));
    con.connector_id = con_id;
    con.modes_ptr = (uint64_t)(uintptr_t)modes;
    con.count_modes = DRM_IDS;
    if (sysio_ioctl(g_fd, DRM_IOCTL_MODE_GETCONNECTOR, &con) != 0) {
        refuse("GETCONNECTOR answered nothing, so no timing source is left");
        return -1;
    }
    if (!con.count_modes || !modes[0].hdisplay || !modes[0].vdisplay) {
        refuse("the connector offers no mode and the crtc holds none - refusing "
               "to invent timings on a DSI bus");
        return -1;
    }
    *out = modes[0];
    return 0;
}

static int find_path(uint32_t *connector_id, uint32_t *crtc_id,
                     struct drm_mode_modeinfo *mode) {
    struct drm_mode_get_resources res;
    uint32_t crtcs[DRM_IDS], connectors[DRM_IDS], encoders[DRM_IDS];
    uint32_t i;

    z(&res, sizeof(res));
    if (sysio_ioctl(g_fd, DRM_IOCTL_MODE_GETRESOURCES, &res) != 0) {
        refuse("GETRESOURCES failed - this card does not speak KMS");
        return -1;
    }
    if (res.count_crtcs > DRM_IDS || res.count_connectors > DRM_IDS ||
        res.count_encoders > DRM_IDS) {
        refuse("the card reports more crtcs/connectors than this path holds");
        return -1;
    }
    z(&res, sizeof(res));
    res.crtc_id_ptr = (uint64_t)(uintptr_t)crtcs;
    res.connector_id_ptr = (uint64_t)(uintptr_t)connectors;
    res.encoder_id_ptr = (uint64_t)(uintptr_t)encoders;
    if (sysio_ioctl(g_fd, DRM_IOCTL_MODE_GETRESOURCES, &res) != 0) {
        refuse("GETRESOURCES refused the id arrays");
        return -1;
    }

    for (i = 0; i < res.count_connectors; i++) {
        struct drm_mode_get_connector con;
        struct drm_mode_get_encoder enc;

        z(&con, sizeof(con));
        con.connector_id = connectors[i];
        if (sysio_ioctl(g_fd, DRM_IOCTL_MODE_GETCONNECTOR, &con) != 0) continue;
        if (con.connection != DRM_MODE_CONNECTED || !con.encoder_id) continue;

        z(&enc, sizeof(enc));
        enc.encoder_id = con.encoder_id;
        if (sysio_ioctl(g_fd, DRM_IOCTL_MODE_GETENCODER, &enc) != 0) continue;
        if (!enc.crtc_id) continue;

        if (find_mode(con.connector_id, enc.crtc_id, mode) != 0) continue;
        *connector_id = con.connector_id;
        *crtc_id = enc.crtc_id;
        return 0;
    }
    refuse("no connected connector with an encoder and a crtc was found");
    return -1;
}

static int build_scanout(uint32_t connector_id, uint32_t crtc_id,
                         const struct drm_mode_modeinfo *mode) {
    struct drm_mode_create_dumb dumb;
    struct drm_mode_fb_cmd add;
    struct drm_mode_crtc set;
    struct drm_mode_map_dumb map;
    struct drm_mode_crtc check;
    uint32_t con_id = connector_id;
    void *p;

    g_w = mode->hdisplay;
    g_h = mode->vdisplay;

    /* 16 bpp, because gui/gfx stores RGB565 in a uint16_t framebuffer. Asking
     * for 32 bpp here would make every pixel drawn land in the wrong place. */
    z(&dumb, sizeof(dumb));
    dumb.width = g_w;
    dumb.height = g_h;
    dumb.bpp = 16;
    if (sysio_ioctl(g_fd, DRM_IOCTL_MODE_CREATE_DUMB, &dumb) != 0) {
        refuse("CREATE_DUMB refused a 16 bpp buffer for the panel's own mode");
        return -1;
    }

    z(&add, sizeof(add));
    add.width = dumb.width;
    add.height = dumb.height;
    add.pitch = dumb.pitch;
    add.bpp = 16;
    add.depth = 16;
    add.handle = dumb.handle;
    if (sysio_ioctl(g_fd, DRM_IOCTL_MODE_ADDFB, &add) != 0) {
        refuse("ADDFB refused the RGB565 buffer");
        return -1;
    }

    z(&set, sizeof(set));
    set.crtc_id = crtc_id;
    set.fb_id = add.fb_id;
    set.count_connectors = 1;
    set.set_connectors_ptr = (uint64_t)(uintptr_t)&con_id;
    set.mode_valid = 1;
    set.mode = *mode;
    if (sysio_ioctl(g_fd, DRM_IOCTL_MODE_SETCRTC, &set) != 0) {
        refuse("SETCRTC was refused - the kernel owns the panel but would not "
               "hand the scanout to this client");
        return -1;
    }

    /* Read the commit back: a vendor driver can return 0 from SETCRTC and keep
     * scanning what it set up itself, and "the ioctl succeeded" is a different
     * statement from "this buffer is what the glass shows". */
    z(&check, sizeof(check));
    check.crtc_id = crtc_id;
    if (sysio_ioctl(g_fd, DRM_IOCTL_MODE_GETCRTC, &check) != 0) {
        umos_logf("panel: the commit is unverified (GETCRTC refused)\n");
    } else if (check.fb_id != add.fb_id) {
        refuse("SETCRTC returned 0 but the crtc still scans another buffer");
        return -1;
    } else {
        umos_logf("panel: crtc %u scans fb %u after SETCRTC\n",
                  (unsigned)crtc_id, (unsigned)check.fb_id);
    }

    z(&map, sizeof(map));
    map.handle = dumb.handle;
    if (sysio_ioctl(g_fd, DRM_IOCTL_MODE_MAP_DUMB, &map) != 0) {
        refuse("MAP_DUMB refused the buffer this file just committed");
        return -1;
    }
    p = sysio_mmap_fd(g_fd, (size_t)(dumb.size ? dumb.size
                                    : (uint64_t)dumb.pitch * g_h),
                      (size_t)map.offset);
    if (!p) {
        refuse("the committed buffer would not map read-write");
        return -1;
    }
    g_map = (uint8_t *)p;
    g_pitch = dumb.pitch;
    g_fb_id = add.fb_id;
    return 0;
}

int violet_panel_init(void) {
    uint32_t connector_id = 0, crtc_id = 0;
    struct drm_mode_modeinfo mode;

    if (g_map) return 0;
    if (ensure_node() != 0) return 0;   /* logs; a dark panel must not kill PID 1 */

    g_fd = sysio_open(DRM_DEV, O_RDWR_);
    if (g_fd < 0) {
        refuse("open /dev/dri/card0 failed - nothing here can modeset");
        return 0;
    }
    /* Best effort: the kernel grants master to the first opener, and a refusal
     * here only tells the log who already holds the card. */
    sysio_ioctl(g_fd, DRM_IOCTL_SET_MASTER, 0);

    z(&mode, sizeof(mode));
    if (find_path(&connector_id, &crtc_id, &mode) != 0) return 0;
    if (build_scanout(connector_id, crtc_id, &mode) != 0) return 0;

    umos_logf("panel: first frame will land at %ux%u, pitch %u, rgb565\n",
              (unsigned)g_w, (unsigned)g_h, (unsigned)g_pitch);
    return 0;
}

int violet_panel_geometry(uint32_t *w, uint32_t *h, uint32_t *stride) {
    if (!g_map) return -1;
    if (w) *w = g_w;
    if (h) *h = g_h;
    if (stride) *stride = g_pitch;      /* bytes: gui/gfx divides by two */
    return 0;
}

uint16_t *violet_panel_fb(void) {
    return (uint16_t *)g_map;
}

/* The dumb buffer is what the CRTC scans, so a painted pixel is on the glass
 * without a copy; there is no second buffer to flip from yet. Reporting a
 * damage rectangle the kernel would ignore is not worse than doing nothing, so
 * this only records that the path is live. */
int violet_panel_flush(int x, int y, int w, int h) {
    (void)x; (void)y; (void)w; (void)h;
    return g_map ? 0 : -1;
}
