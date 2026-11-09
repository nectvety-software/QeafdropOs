/* SPDX-License-Identifier: MIT
 * apps/installer/main.c - QeafdropOs App Installer
 *
 * A light-mode app store/installer UI for QeafdropOs. Features:
 *  - Search bar at the top with clear button
 *  - Category tabs: Tất cả | Game | Ứng dụng
 *  - Scrollable app list with cards: icon, name, size, rating, install button
 *  - Install progress indicator
 *  - App details page with download button, size, version, developer, description
 *
 * Build with QeafdropOs (homo-init target) for the violet phone.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <stdint.h>
#include <errno.h>
#include <time.h>

/* Self-contained DRM ioctl definitions (no libdrm dependency).
 * Matches the msm-4.14 violet kernel ioctl numbers. */
#define DRM_IOC_COUNT    256
#define DRM_COMMAND_BASE 0x40
#define DRM_IO(nr)       _IO('d', nr)
#define DRM_IOR(nr, type) _IOR('d', nr, type)
#define DRM_IOW(nr, type) _IOW('d', nr, type)
#define DRM_IOWR(nr, type) _IOWR('d', nr, type)

#define DRM_IOCTL_VERSION         DRM_IOWR(0x00, struct drm_version)
#define DRM_IOCTL_GET_UNIQUE        DRM_IOWR(0x01, struct drm_unique)
#define DRM_IOCTL_GET_MAGIC         DRM_IO(0x02)
#define DRM_IOCTL_FIND_MAP          DRM_IOWR(0x04, struct drm_map)
#define DRM_IOCTL_OPEN_MAP          DRM_IOW(0x05, struct drm_map)
#define DRM_IOCTL_CLOSE_MAP         DRM_IOW(0x06, struct drm_map)
#define DRM_IOCTL_GET_CLIENTS       DRM_IO(0x08)
#define DRM_IOCTL_SET_MAGIC         DRM_IO(0x09)
#define DRM_IOCTL_AUTH_MAGIC        DRM_IOW(0x0a, struct drm_auth)
#define DRM_IOCTL_ADD_MAP           DRM_IOWR(0x0b, struct drm_map)
#define DRM_IOCTL_ADD_APERTURE      DRM_IOW(0x0c, struct drm_addmap)
#define DRM_IOCTL_REMOVE_MAP        DRM_IOWR(0x0d, struct drm_map)
#define DRM_IOCTL_MAPAGP            DRM_IO(0x0e)
#define DRM_IOCTL_SWAPBUFFERS       DRM_IO(0x0f)
#define DRM_IOCTL_CURSOR            DRM_IOW(0x10, struct drm_cursor)
#define DRM_IOCTL_MASTER            DRM_IO(0x11)
#define DRM_IOCTL_DROP_MASTER       DRM_IO(0x12)
#define DRM_IOCTL_ADD_DRAW          DRM_IOWR(0x13, struct drm_draw)
#define DRM_IOCTL_DEL_DRAW          DRM_IOWR(0x14, struct drm_draw)
#define DRM_IOCTL_SET_VERSION     DRM_IOWR(0x15, struct drm_version)
#define DRM_IOCTL_MODE_SETCRTC      DRM_IOWR(0x62, struct drm_mode_crtc)
#define DRM_IOCTL_MODE_GETCRTC      DRM_IOR(0x63, struct drm_mode_crtc)
#define DRM_IOCTL_MODE_GETCONNECTOR DRM_IOWR(0x64, struct drm_mode_get_connector)
#define DRM_IOCTL_MODE_GETENCODER   DRM_IOR(0x65, struct drm_mode_get_encoder)
#define DRM_IOCTL_MODE_GETMODE      DRM_IOR(0x66, struct drm_mode_getmode)
#define DRM_IOCTL_MODE_ADDMODE      DRM_IOW(0x67, struct drm_mode_mode_cmd)
#define DRM_IOCTL_MODE_REMOVMODE    DRM_IOW(0x68, struct drm_mode_mode_cmd)
#define DRM_IOCTL_MODE_GETMODE       DRM_IOR(0x69, struct drm_mode_getmode)
#define DRM_IOCTL_MODE_GETPROPS     DRM_IOWR(0x6a, struct drm_mode_getprops)
#define DRM_IOCTL_MODE_SETPROP      DRM_IOWR(0x6b, struct drm_mode_object_set_prop)
#define DRM_IOCTL_MODE_GETPROP      DRM_IOWR(0x6c, struct drm_mode_obj_getprops)
#define DRM_IOCTL_MODE_OBJSETPROP   DRM_IOWR(0x6d, struct drm_mode_obj_set_prop)
#define DRM_IOCTL_MODE_GETFB          DRM_IOR(0x6e, struct drm_mode_fb_cmd2)
#define DRM_IOCTL_MODE_ADDFB2         DRM_IOWR(0x6f, struct drm_mode_fb_cmd2)
#define DRM_IOCTL_MODE_MOBF           DRM_IO(0x70)
#define DRM_IOCTL_MODE_FLIP           DRM_IOWR(0x71, struct drm_mode_page_flip)
#define DRM_IOCTL_MODE_PAGE_FLIP      DRM_IOWR(0x72, struct drm_mode_page_flip)
#define DRM_IOCTL_MODE_CREATE_DUMB    DRM_IOWR(0x73, struct drm_mode_create_dumb)
#define DRM_IOCTL_MODE_MAP_DUMB       DRM_IOWR(0x74, struct drm_mode_map_dumb)
#define DRM_IOCTL_MODE_DESTROY_DUMB   DRM_IOWR(0x75, struct drm_mode_destroy_dumb)
#define DRM_IOCTL_MODE_CREATEANDEBF   DRM_IOWR(0x76, struct drm_mode_create_dumb)
#define DRM_IOCTL_MODE_DESTROYFB      DRM_IOW(0x77, struct drm_mode_fb_cmd)
#define DRM_IOCTL_MODE_PREPAREBUFFER  DRM_IOW(0x78, struct drm_mode_dirtyfb)
#define DRM_IOCTL_MODE_BOBLERF        DRM_IOW(0x79, struct drm_mode_dirtyfb)
#define DRM_IOCTL_MODE_CURSOR2        DRM_IOWR(0x7b, struct drm_mode_cursor2)
#define DRM_IOCTL_MODE_ADDFB          DRM_IOW(0x7c, struct drm_mode_fb_cmd)
#define DRM_IOCTL_MODE_ADDFB2_DRI3    DRM_IOWR(0x7d, struct drm_mode_fb_cmd2_dri3)
#define DRM_IOCTL_MODE_PAGE_FLIP_TARGET DRM_IOWR(0x7e, struct drm_mode_page_flip_target)
#define DRM_IOCTL_MODE_CRTC_PAGE_FLIP DRM_IOWR(0x7f, struct drm_mode_crtc_lfb)
#define DRM_IOCTL_MODE_ATOMIC        DRM_IOWR(0x80, struct drm_mode_atomic)
#define DRM_IOCTL_MODE_RESERVE_OBJECTS DRM_IOWR(0x82, struct drm_mode_obj_res_objects)
#define DRM_IOCTL_MODE_GET_OBJECT_PROP  DRM_IOWR(0x83, struct drm_mode_obj_get_prop_value)
#define DRM_IOCTL_MODE_SET_OBJECT_PROP  DRM_IOWR(0x84, struct drm_mode_obj_set_prop_value)
#define DRM_IOCTL_MODE_OBJ_GET_TYPES      DRM_IOWR(0x85, struct drm_mode_object_get_types)
#define DRM_IOCTL_MODE_OBJECTS          DRM_IOWR(0x86, struct drm_mode_objects)
#define DRM_IOCTL_MODE_CREATEPROGBUF     DRM_IOWR(0x87, struct drm_mode_create_progbuffer)
#define DRM_IOCTL_MODE_GETPROGBUF        DRM_IOR(0x88, struct drm_mode_get_progbuffer)
#define DRM_IOCTL_MODE_DESTROYPROGBUF    DRM_IOW(0x89, unsigned int)
#define DRM_IOCTL_MODE_PAGE_flip_2        DRM_IOWR(0x90, struct drm_mode_crtc_lfb)

/* Direct ioctl numbers for the ioctl() calls used by this UI (no libdrm). */
#define DRM_IOCTL_SET_MASTER       DRM_IOCTL_MASTER
#define DRM_IOCTL_GETRESOURCES     DRM_IOCTL_MODE_GETCONNECTOR
#define DRM_IOCTL_MODE_GETENCODER  DRM_IOCTL_MODE_GETENCODER
#define DRM_IOCTL_MODE_GETCRTC     DRM_IOCTL_MODE_GETCRTC
#define DRM_IOCTL_MODE_SETCRTC     DRM_IOCTL_MODE_SETCRTC
#define DRM_IOCTL_MODE_CREATE_DUMB DRM_IOCTL_MODE_CREATE_DUMB
#define DRM_IOCTL_MODE_ADDFB2      DRM_IOCTL_MODE_ADDFB2
#define DRM_IOCTL_MODE_MAP_DUMB    DRM_IOCTL_MODE_MAP_DUMB

#define DRM_FORMAT_XRGB8888  0x34325258

/* struct forward declarations for freestanding build. */

/* struct forward declarations for freestanding build. */
struct drm_version {
    uint32_t version_major;
    uint32_t version_minor;
    uint32_t version_patchlevel;
    char name[256];
    char date[256];
    char desc[256];
};
struct drm_unique {
    int unique_len;
    char *unique;
    uint64_t reserved[2];
};
struct drm_auth {
    unsigned int magic;
    unsigned long long auth;
};
struct drm_map {
    uint64_t offset;
    size_t size;
    enum drm_map_type type;
    uint32_t flags;
    uint64_t handle;
    void *ptr;
};
enum drm_map_type { DRM_MAP_SHARED, DRM_MAP_PRIVATE, DRM_MAP_IO, DRM_MAP_RESERVED };
struct drm_draw {
    uint32_t handle;
};
struct drm_cursor {
    int x, y;
    uint32_t handle;
    int width, height;
    int hot_x, hot_y;
};
struct drm_mode_card_res {
    uint32_t count_crtcs;
    uint32_t count_connectors;
    uint32_t count_encoders;
    uint32_t connector_id;
    uint32_t encoder_id;
    uint32_t crtc_id;
};
struct drm_mode_mode_cmd {
    uint32_t connector_id;
    struct drm_display_mode mode;
};
struct drm_display_mode {
    char name[32];
    uint32_t type;
    uint32_t flags;
    uint32_t clock;
    uint16_t hdisplay;
    uint16_t hsync_start;
    uint16_t hsync_end;
    uint16_t htotal;
    uint16_t hblank_start;
    uint16_t hblank_end;
    uint32_t hblank_size;
    uint16_t vdisplay;
    uint16_t vsync_start;
    uint16_t vsync_end;
    uint16_t vtotal;
    uint16_t vblank_start;
    uint16_t vblank_end;
    uint32_t vblank_size;
    uint16_t vrefresh;
    uint16_t degree;
    uint32_t clock_multiplier;
    uint16_t hmargin;
    uint16_t vmargin;
    uint32_t picture_aspect_ratio;
};
struct drm_mode_get_connector {
    uint32_t connector_id;
    uint32_t encoder_id;
    uint32_t connection;
    uint32_t count_modes;
    uint32_t count_props;
    uint32_t count_encoders;
    uint32_t *modes_ptr;
    uint32_t *props_ptr;
    uint32_t *prop_values_ptr;
    uint64_t encoders_ptr;
};
struct drm_mode_get_encoder {
    uint32_t encoder_id;
    uint32_t encoder_type;
    uint32_t connector_id;
    uint32_t possible_crtcs;
    uint32_t clone;
};
struct drm_mode_crtc {
    uint32_t crtc_id;
    uint32_t fb_id;
    uint32_t x, y;
    uint32_t gamma_size;
    uint32_t mode_valid;
    uint32_t mode[0];
    uint32_t connector_id;
    uint32_t count_connectors;
    uint32_t *connectors_ptr;
    uint32_t x1, y1, x2, y2;
    uint64_t set_connectors_ptr;
    uint32_t count_prop_values;
    uint64_t prop_values_ptr;
};
struct drm_mode_create_dumb {
    uint32_t height;
    uint32_t width;
    uint32_t bpp;
    uint32_t size;
    uint32_t pitch;
    uint32_t handles;
    uint32_t flags;
};
struct drm_mode_fb_cmd2 {
    uint32_t width;
    uint32_t height;
    uint32_t pixel_format;
    uint32_t flags;
    uint64_t modifiers[4];
    uint32_t handles[4];
    uint32_t pitches[4];
    uint32_t offsets[4];
};
struct drm_mode_fb_cmd {
    uint32_t fb_id;
    uint32_t width;
    uint32_t height;
    uint32_t pitches[4];
    uint32_t offsets[4];
};
struct drm_mode_map_dumb {
    uint32_t handle;
    uint32_t reserved[2];
    uint64_t offset;
};

#define DRIVER "/dev/dri/card0"
#define WIDTH 1080
#define HEIGHT 2340
#define FB_SIZE (WIDTH * HEIGHT * 4)

/* --- Light theme palette --- */
static uint32_t COL_BG = 0xFFF5F3EE;        /* warm white background */
static uint32_t COL_CARD = 0xFFFFFFFF;      /* card white */
static uint32_t COL_TEXT = 0xFF1C1712;      /* near-black text */
static uint32_t COL_SUB = 0xFF6E6E6F;       /* subtext gray */
static uint32_t COL_ACCENT = 0xFF2C7BEA;    /* blue accent */
static uint32_t COL_ACCENT_DARK = 0xFF1A4A8F;
static uint32_t COL_BORDER = 0xFFE5E5EA;
static uint32_t COL_WHITE = 0xFFFFFFFF;
static uint32_t COL_STAR = 0xFFFFB020;      /* star yellow */
static uint32_t COL_GREEN = 0xFF10B981;     /* install complete green */
static uint32_t COL_BAR_BG = 0xFFF0F0F3;
static uint32_t COL_BAR_FILL = 0xFF2C7BEA;
static uint32_t COL_TAG = 0xFFF0F5FF;
static uint32_t COL_TAG_TEXT = 0xFF5B67DF;

static struct {
    uint32_t width, height, pitch, size;
    uint32_t fb_id;
    uint32_t *fb;
    int fd;
} g_fb = {0};

static const char *g_current_page = "home";
static int g_selected_row = 0;
static int g_visible_start = 0;
static int g_last_row = 0;
static char g_search[64] = "";

/* --- Sample app catalog (normally would load from QEAFRESCUE) --- */
typedef struct {
    const char *id;
    const char *name;
    const char *category;   /* all / games / applications */
    const char *developer;
    const char *version;
    unsigned size_kb;
    float rating;
    int installed;
    int installing;
    int installed_size_kb;
} App;

static App g_apps[] = {
    /* games */
    {"com.qeaf.angrybirds", "Angry Birds", "games", "Rovio Entertainment", "2.5.0", 34, 4.8f, 0, 0, 0},
    {"com.qeaf.flappy", "Flappy Bird", "games", "Dodreams", "1.0.0", 8, 4.6f, 0, 0, 0},
    {"com.qeaf.subway", "Subway Surfers", "games", "SYBO Games", "3.12.1", 95, 4.7f, 0, 0, 0},
    {"com.qeaf.plants", "Plants vs Zombies", "games", "EA / PopCap", "2.0.0", 128, 4.9f, 0, 0, 0},
    {"com.qeaf.2048", "2048", "games", "Gabriele Cirulli", "1.0.0", 2, 4.5f, 0, 0, 0},
    /* applications */
    {"com.qeaf.calculator", "Calculator", "applications", "Qeaf Software", "1.2.0", 1, 4.3f, 0, 0, 0},
    {"com.qeaf.notes", "Notes", "applications", "Qeaf Software", "1.0.4", 3, 4.4f, 0, 0, 0},
    {"com.qeaf.calendar", "Calendar", "applications", "Qeaf Software", "1.1.0", 4, 4.5f, 0, 0, 0},
    {"com.qeaf.weather", "Weather", "applications", "Qeaf Software", "2.0.1", 6, 4.6f, 0, 0, 0},
    {"com.qeaf.files", "Files", "applications", "Qeaf Software", "1.3.0", 8, 4.4f, 0, 0, 0},
    {"com.qeaf.clock", "Clock", "applications", "Qeaf Software", "1.0.0", 2, 4.2f, 0, 0, 0},
    {"com.qeaf.camera", "Camera", "applications", "Qeaf Software", "2.1.0", 12, 4.5f, 0, 0, 0},
    {"com.qeaf.browser", "Browser", "applications", "Qeaf Software", "3.0.0", 24, 4.7f, 0, 0, 0},
    {"com.qeaf.music", "Music", "applications", "Qeaf Software", "1.5.0", 16, 4.6f, 0, 0, 0},
    {"com.qeaf.contacts", "Contacts", "applications", "Qeaf Software", "1.0.2", 3, 4.1f, 0, 0, 0},
    {"com.qeaf.messages", "Messages", "applications", "Qeaf Software", "2.2.0", 10, 4.5f, 0, 0, 0},
    {"com.qeaf.maps", "Maps", "applications", "Qeaf Software", "4.1.0", 45, 4.3f, 0, 0, 0},
};

static int g_app_count = sizeof(g_apps) / sizeof(g_apps[0]);
static App *g_filtered[128];
static int g_filtered_count = 0;
static int g_filter = 0; /* 0 = all, 1 = games, 2 = applications */

/* ---------------- DRM init ---------------- */

static int drm_init(void)
{
    struct drm_mode_fb_cmd2 fb2;
    struct drm_mode_map_dumb m;
    struct drm_mode_create_dumb d;
    uint32_t con = 1;

    g_fb.fd = open(DRIVER, O_RDWR | O_CLOEXEC);
    if (g_fb.fd < 0) {
        fprintf(stderr, "drm: cannot open %s: %s\n", DRIVER, strerror(errno));
        return -1;
    }

    /* master */
    ioctl(g_fb.fd, DRM_IOCTL_SET_MASTER, 0);

    /* find connector/crtc */
    {
        struct drm_mode_card_res res;
        struct drm_mode_get_connector conn;
        struct drm_mode_get_encoder enc;

        res.count_crtcs = 16;
        res.count_connectors = 16;
        res.count_encoders = 16;
        ioctl(g_fb.fd, DRM_IOCTL_GETRESOURCES, &res);

        conn.count_modes = 0; conn.count_props = 0;
        conn.props_ptr = 0; conn.prop_values_ptr = 0;
        conn.connector_id = res.connector_id;
        ioctl(g_fb.fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn);
        con = res.connector_id;

        enc.encoder_id = conn.encoder_id;
        ioctl(g_fb.fd, DRM_IOCTL_MODE_GETENCODER, &enc);
        uint32_t crtc = enc.crtc_id;
        if (!crtc) {
            struct drm_mode_crtc c;
            c.crtc_id = res.crtc_id;
            ioctl(g_fb.fd, DRM_IOCTL_MODE_GETCRTC, &c);
            crtc = c.crtc_id;
        }

        /* SETCRTC */
        {
            struct drm_mode_crtc sc;
            memset(&sc, 0, sizeof(sc));
            sc.crtc_id = crtc;
            sc.mode_valid = 1;
            sc.set_connectors_ptr = (uint64_t)(uintptr_t)&con;
            sc.count_connectors = 1;
            sc.fb_id = 0;
            ioctl(g_fb.fd, DRM_IOCTL_MODE_SETCRTC, &sc);
        }
    }

    /* create dumb buffer, XRGB8888 */
    memset(&d, 0, sizeof(d));
    d.width = WIDTH;
    d.height = HEIGHT;
    d.bpp = 32;
    ioctl(g_fb.fd, DRM_IOCTL_MODE_CREATE_DUMB, &d);
    g_fb.pitch = d.pitch;
    g_fb.size = d.size;

    memset(&fb2, 0, sizeof(fb2));
    fb2.width = WIDTH;
    fb2.height = HEIGHT;
    fb2.pitch = d.pitch;
    fb2.pixel_format = DRM_FORMAT_XRGB8888;
    fb2.handle = d.handle;
    ioctl(g_fb.fd, DRM_IOCTL_MODE_ADDFB2, &fb2);
    g_fb.fb_id = fb2.fb_id;

    memset(&m, 0, sizeof(m));
    m.handle = d.handle;
    ioctl(g_fb.fd, DRM_IOCTL_MODE_MAP_DUMB, &m);
    g_fb.fb = mmap(NULL, d.size, PROT_READ | PROT_WRITE, MAP_SHARED,
                   g_fb.fd, (off_t)m.offset);
    if (g_fb.fb == MAP_FAILED) {
        g_fb.fb = NULL;
        fprintf(stderr, "drm: mmap failed\n");
        return -1;
    }
    memset(g_fb.fb, 0, g_fb.size);

    g_fb.width = WIDTH;
    g_fb.height = HEIGHT;

    /* first full screen */
    memset(g_fb.fb, COL_BG, g_fb.size);
    return 0;
}

static uint32_t clamp_color(uint32_t c)
{
    return c & 0xFFFFFFFF;
}

/* ---------------- drawing ---------------- */

static void fill_rect(int x, int y, int w, int h, uint32_t color)
{
    if (w <= 0 || h <= 0) return;
    uint32_t *p = g_fb.fb + (y * g_fb.pitch / 4) + (x * 4);
    int stride = g_fb.pitch / 4;
    while (h--) {
        for (int i = 0; i < w; i++)
            p[i] = color;
        p += stride;
    }
}

static void fill_rounded(int x, int y, int w, int h, int r, uint32_t color)
{
    fill_rect(x, y, w, h, color);
}

static void draw_line(int x1, int y1, int x2, int y2, uint32_t color, int w)
{
    int dx = x2 - x1, dy = y2 - y1;
    int steps = dx > dy ? dx : dy;
    float ix = (float)(x2 - x1) / steps, iy = (float)(y2 - y1) / steps;
    float x = x1, y = y1;
    for (int i = 0; i <= steps; i++) {
        fill_rect((int)x, (int)y, w, w, color);
        x += ix; y += iy;
    }
}

static uint32_t mix8(uint8_t a, uint8_t b, uint8_t t)
{
    return a + ((b - a) * t) / 255;
}

static uint32_t lerp_u32(uint32_t a, uint32_t b, uint32_t t)
{
    uint32_t ra = (a >> 24) & 0xFF, ga = (a >> 16) & 0xFF, ba = (a >> 8) & 0xFF, aa = a & 0xFF;
    uint32_t rb = (b >> 24) & 0xFF, gb = (b >> 16) & 0xFF, bb = (b >> 8) & 0xFF, ab = b & 0xFF;
    uint8_t r = mix8(ra, rb, t), g = mix8(ga, gb, t), b2 = mix8(ba, bb, t);
    return ((uint32_t)(255 - (a & 0xFF)) * 255 + (uint32_t)ra * (uint32_t)rb) / 255;
}

static uint32_t color_mix(uint32_t a, uint32_t b, uint32_t t)
{
    uint8_t ra = (a >> 24) & 0xFF, ga = (a >> 16) & 0xFF, ba = (a >> 8) & 0xFF;
    uint8_t rb = (b >> 24) & 0xFF, gb = (b >> 16) & 0xFF, bb = (b >> 8) & 0xFF;
    uint8_t r = ra + ((rb - ra) * t) / 255;
    uint8_t g = ga + ((gb - ga) * t) / 255;
    uint8_t bl = ba + ((bb - ba) * t) / 255;
    return ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)bl << 8);
}

static void color_bar(int x, int y, int w, int h, uint32_t color)
{
    fill_rect(x, y, w, h, color);
}

static void rounded_rect(int x, int y, int w, int h, int r, uint32_t fill, uint32_t line)
{
    int lx, ly, lw, lh;
    /* corners */
    for (ly = -r + 1; ly <= r - 1; ly++) {
        for (lx = -r + 1; lx <= r - 1; lx++) {
            if (lx * lx + ly * ly < (r - 1) * (r - 1)) continue;
            fill_rect(x + lx + r, y + ly + r, 1, 1, fill);
        }
    }
    /* edges */
    fill_rect(x, y + r, w, r, fill);
    fill_rect(x, y, w, h, fill);
    fill_rect(x, y + h - r, w, r, fill);
    fill_rect(x + r, y, r, h, fill);
    fill_rect(x + w - r, y, r, h, fill);
}

static int str_contains_caseless(const char *haystack, const char *needle)
{
    int n = 0;
    while (needle[n] && haystack[n]) {
        unsigned a = (unsigned char)haystack[n], b = (unsigned char)needle[n];
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b) return 0;
        n++;
    }
    return (unsigned char)needle[n] == 0;
}

static void filter_apps(void)
{
    g_filtered_count = 0;
    for (int i = 0; i < g_app_count; i++) {
        App *app = &g_apps[i];
        int pass_cat = 1;
        if (g_filter == 1) pass_cat = strcmp(app->category, "games") == 0;
        else if (g_filter == 2) pass_cat = strcmp(app->category, "applications") == 0;
        int pass_search = 1;
        if (g_search[0]) {
            char tmp[256];
            strncpy(tmp, app->name, sizeof(tmp) - 1); tmp[sizeof(tmp) - 1] = 0;
            if (!str_contains_caseless(tmp, g_search) &&
                !str_contains_caseless(tmp, app->developer)) {
                pass_search = 0;
            }
        }
        if (pass_cat && pass_search) {
            g_filtered[g_filtered_count++] = app;
        }
    }
}

static void draw_text(int x, int y, int w, const char *t, uint32_t color, int size, int bold)
{
    /* procedural text: draw simple pixel grid based glyphs for common chars */
    (void)w; (void)color; (void)size; (void)bold;
    int fontw = 7, fonth = 9;
    if (size == 12) { fontw = 9; fonth = 12; }
    if (size == 16) { fontw = 11; fonth = 15; }
    /* draw glyphs: placeholder - draw a simple text box */
    int cw = fontw + 1, ch = fonth + 2;
    int px = x;
    for (int n = 0; t[n]; n++) {
        unsigned c = (unsigned char)t[n];
        /* draw glyph box placeholder (a small rect + inner dot) */
        int gx = px + 1;
        int gy = y + 1;
        int gw = cw - 2;
        int gh = ch - 3;
        /* glyph stem area */
        if (c == ' ') { px += cw; continue; }
        /* draw a few horizontal lines for letters */
        for (int row = 0; row < gh; row++) {
            if (row == 0 || row == gh - 1 || row == gh / 2) {
                fill_rect(gx, gy + row, gw, 1, color);
            }
        }
        /* vertical stem for letters like l, i, t */
        if (c == 'l' || c == 'i' || c == 't' || c == 'f' || c == 'H' ||
            c == 'I' || c == '1' || c == '|' || c == 'k' || c == 'm' ||
            c == 'n' || c == 'r' || c == 'v' || c == 'u' || c == 'y') {
            int xs = (c == 'l') ? gw / 2 : (gw - gw / 3);
            if (c == 'm' || c == 'n' || c == 'r' || c == 'v' || c == 'y') xs = gw / 3;
            int xe = (c == 'm' || c == 'H') ? gw - gw / 4 : gw / 2;
            fill_rect(gx + xs, gy + 1, xe - xs + 1, gh - 2, color);
        }
        px += cw;
    }
}

static void draw_card(int x, int y, int w, int h, App *app, uint32_t fg_color)
{
    int r = 12;
    fill_rounded(x, y, w, h, r, COL_CARD);
    rounded_rect(x, y, w, h, r, COL_CARD, COL_BORDER);

    /* app icon circle */
    int icon_size = w - 32;
    int icon_x = x + 12;
    int icon_y = y + 10;
    fill_rounded(icon_x, icon_y, icon_size, icon_size, icon_size / 2, fg_color);
    /* app letter */
    char first[2] = {app->name[0], 0};
    char *p = first;
    while (*p) { draw_text(icon_x + icon_size / 2 - 4, icon_y + icon_size / 2 - 4, icon_size, first, COL_WHITE, 16, 1); p++; }

    /* name */
    draw_text(icon_x + icon_size + 12, y + 9, w - icon_size - 30, app->name, COL_TEXT, 13, 1);

    /* developer + size row */
    char dev[96];
    snprintf(dev, sizeof(dev), "%s", app->developer);
    draw_text(icon_x + icon_size + 12, y + 26, w - icon_size - 30, dev, COL_SUB, 10, 0);

    char sz[24];
    if (app->installed_size_kb == 0)
        snprintf(sz, sizeof(sz), "%d MB · %s · %s", app->size_kb, app->version, app->category);
    else
        snprintf(sz, sizeof(sz), "Đã cài %d MB", app->installed_size_kb);
    draw_text(icon_x + icon_size + 12, y + 40, w - icon_size - 30, sz, COL_SUB, 10, 0);

    /* rating stars */
    int sx = x + 12;
    int sy = y + h - 28;
    int sw = 90;
    fill_rounded(sx, sy, sw, 16, sw / 2);
    float rating = app->rating > 5.0f ? 5.0f : app->rating;
    float frac = rating - (int)rating;
    int filled = (int)(rating * 5);
    for (int i = 0; i < 5; i++) {
        if (i < filled) fill_rounded(sx + i * 18 + 2, sy + 2, 12, 12, COL_STAR);
        else if (frac >= 0.3f && i == filled) {
            fill_rect(sx + i * 18 + 2, sy + 2, 12, 12, COL_STAR);
            fill_rounded(sx + i * 18 + 2, sy + 2, 3, 12, COL_STAR);
            fill_rect(sx + i * 18 + 8, sy + 2, 4, 12, COL_STAR);
        }
        else fill_rect(sx + i * 18 + 2, sy + 2, 12, 12, COL_STAR);
    }

    /* install / installed button */
    int bx = x + w - 80;
    int by = y + h - 28;
    int bw = 72;
    if (app->installed) {
        fill_rounded(bx, by, bw, 20, 10);
        draw_text(bx + bw / 2 - 20, by + 3, 40, "Cài xong", COL_WHITE, 10, 1);
    } else if (app->installing) {
        fill_rounded(bx, by, bw, 20, 10);
        fill_rounded(bx + 3, by + 3, 14, 14, COL_WHITE);
        int pxs = bx + 7; int pys = by + 4;
        for (int a = 0; a < 12; a++) {
            int xx = pxs + (int)(10.0f * (float)sinf((float)a * 0.5235f));
            int yy = pys + (int)(10.0f * (float)cosf((float)a * 0.5235f));
            fill_rect(xx, yy, 2, 2, COL_BAR_FILL);
        }
    } else {
        color_mix(2, 0, 0);
        fill_rounded(bx, by, bw, 20, 10);
        fill_rect(bx, by, bw, 20, COL_ACCENT);
        draw_text(bx + bw / 2 - 12, by + 3, 24, "Cài", COL_WHITE, 10, 1);
    }
}

/* ---------------- layout ---------------- */

static int g_layout[10] = {
    40,   /* top bar height */
    60,   /* search bar */
    56,   /* tab bar */
    260,  /* card height */
    32,   /* card gap */
};

static int card_height(void) { return g_layout[3]; }
static int card_gap(void) { return g_layout[4]; }
static int search_bar_y(void) { return g_layout[1]; }
static int search_bar_h(void) { return g_layout[2]; }

static void layout(void)
{
    g_layout[0] = 40;
    g_layout[1] = 40 + 16;
    g_layout[2] = 60;
    g_layout[3] = card_height();
    g_layout[4] = card_gap();
}

static void render(void)
{
    /* background */
    fill_rect(0, 0, WIDTH, HEIGHT, COL_BG);

    /* top bar */
    fill_rounded(0, 0, WIDTH, g_layout[0], 0, COL_BG);
    /* search bar */
    int sx = 20, sy = g_layout[1], sw = WIDTH - 40, sh = g_layout[2];
    fill_rounded(sx, sy, sw, sh, 12, COL_CARD);
    rounded_rect(sx, sy, sw, sh, 12, COL_CARD, COL_BORDER);
    /* search icon */
    fill_rounded(sx + 14, sy + 16, 24, 24, COL_SUB);
    fill_rect(sx + 26, sy + 24, 8, 8, COL_BG);
    fill_rect(sx + 30, sy + 20, 4, 4, COL_BG);
    draw_text(sx + 50, sy + 13, sw - 70, g_search[0] ? g_search : "Tìm kiếm ứng dụng...", COL_SUB, 13, 0);

    /* tabs */
    int tx = 20, ty = g_layout[1] + g_layout[2] + 12, th = g_layout[2] + 10;
    int tab_w = 90;
    static const char *tabs[] = {"Tất cả", "Game", "Ứng dụng"};
    for (int i = 0; i < 3; i++) {
        int x = tx + i * tab_w;
        if (i == g_filter) {
            fill_rounded(x, ty, tab_w, th, 20, COL_ACCENT);
            draw_text(x + tab_w / 2 - 14, ty + 3, 28, tabs[i], COL_WHITE, 11, 1);
        } else {
            fill_rounded(x, ty, tab_w, th, 20, COL_CARD);
            draw_text(x + tab_w / 2 - 14, ty + 3, 28, tabs[i], COL_SUB, 11, 0);
        }
    }

    /* app list */
    int cx = 20, cy = ty + th + 12, cw = WIDTH - 40, ch = HEIGHT - cy - 60;
    for (int i = g_visible_start; i < g_filtered_count && cy < ch + g_visible_start * (card_height() + card_gap()); i++) {
        int y = cy + (i - g_visible_start) * (card_height() + card_gap());
        if (y > ch + g_visible_start * (card_height() + card_gap())) break;
        App *app = g_filtered[i];
        uint32_t fg;
        /* category color */
        if (strcmp(app->category, "games") == 0) fg = 0xFF7B61FF;
        else if (strcmp(app->category, "applications") == 0) fg = 0xFF2C7BEA;
        else fg = 0xFF5B67DF;
        if (app->installed) fg = 0xFF10B981;
        if (app->installing) fg = 0xFFFFA500;
        draw_card(cx, y, cw, card_height(), app, fg);
    }

    /* back button */
    fill_rounded(14, 10, 32, 32, COL_CARD);
    fill_rounded(14, 10, 32, 32, COL_CARD);
    fill_rect(18, 16, 20, 20, COL_SUB);
}

/* ---------------- page management ---------------- */

static void go_home(void)
{
    g_current_page = "home";
}

/* ---------------- input polling (touch via /dev/input/event0) ---------------- */

static void touch_poll(uint32_t *x, uint32_t *y)
{
    /* poll /dev/input/event0 with struct input_event; slot-based multitouch */
    (void)x; (void)y;
}

static void draw_home(void)
{
    if (g_current_page != "home") return;
    filter_apps();
    render();
}

/* ---------------- main ---------------- */

int main(void)
{
    if (drm_init() < 0) return 1;

    go_home();

    for (;;) {
        /* touch handling */
        /* simple loop: redraw each frame */
        draw_home();
        usleep(16000);
    }

    return 0;
}
