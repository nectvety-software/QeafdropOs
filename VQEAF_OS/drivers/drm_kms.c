/* SPDX-License-Identifier: MIT */
/* platform/violet/drm_kms.c - DRM/KMS backend without libdrm.
 *
 * The violet kernel has CONFIG_DRM_MSM=y and no fbdev emulation at all
 * (# CONFIG_DRM_FBDEV_EMULATION is not set), so LVGL's fbdev driver cannot be
 * used. This is the smallest path that works on msm-4.14: legacy GETRESOURCES/
 * GETCONNECTOR/GETENCODER, a dumb XRGB8888 buffer, ADDFB and SETCRTC, then
 * DIRTYFB for every partial update.
 */
#define _GNU_SOURCE

#include "drm_kms.h"

#include "violet.h"

#include <drm/drm.h>
#include <drm/drm_fourcc.h>

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

/* struct drm_mode_get_connector's `connection` values; this toolchain's
 * drm_mode.h predates the enum names (same gap the QeafdropOs tree hit). */
#define VIO_CONN_CONNECTED    1
#define VIO_CONN_DISCONNECTED 2

/* The DSI connector on this board has answered "disconnected" while the card
 * was otherwise healthy - measured 2026-11-09, the same kernel+DTB reported
 * connector 28 connected at 0.96 s on one boot and never enabled DSI on the
 * next - so a single ask is not a verdict. Poll, and after a grace period
 * modeset the best connector that still lists the panel's mode anyway. */
#define DRM_SCAN_POLL_MS 500
#define DRM_SCAN_TRIES   60    /* 30 s of polling before the verdict */
#define DRM_FORCE_AFTER  20    /* ~10 s: modeset the best modeful connector */

static const char *conn_state_name(uint32_t connection)
{
    switch(connection) {
        case VIO_CONN_CONNECTED:    return "on";
        case VIO_CONN_DISCONNECTED: return "off";
        default:                    return "?";
    }
}

/* Which connector to drive when several list a mode: the panel's DSI bus
 * first, then embedded ports, then anything else. */
static int conn_rank(uint32_t type)
{
    switch(type) {
        case DRM_MODE_CONNECTOR_DSI:   return 3;
        case DRM_MODE_CONNECTOR_eDP:
        case DRM_MODE_CONNECTOR_DisplayPort:
        case DRM_MODE_CONNECTOR_HDMIA:
        case DRM_MODE_CONNECTOR_HDMIB: return 2;
        default:                       return 1;
    }
}

static void census_add(char *buf, size_t sz, const char *fmt, ...)
{
    size_t used = strlen(buf);
    va_list ap;

    if(used >= sz) return;
    va_start(ap, fmt);
    vsnprintf(buf + used, sz - used, fmt, ap);
    va_end(ap);
}

static int drm_open_card(void)
{
    static const char *const candidates[] = {
        "/dev/dri/card0", "/dev/dri/card1", "/dev/dri/card2"
    };
    for(size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        int fd = open(candidates[i], O_RDWR | O_CLOEXEC);
        if(fd >= 0) {
            vio_log("drm: opened %s", candidates[i]);
            return fd;
        }
    }
    return -1;
}

static int pick_mode(const struct drm_mode_modeinfo *modes, uint32_t count,
                     const struct drm_mode_modeinfo *current,
                     int prefer_w, int prefer_h,
                     struct drm_mode_modeinfo *out)
{
    uint32_t i;
    int best = -1;
    long best_area = -1;

    for(i = 0; i < count; i++) {
        if(modes[i].hdisplay == prefer_w && modes[i].vdisplay == prefer_h) {
            *out = modes[i];
            return 0;
        }
        if(modes[i].type & DRM_MODE_TYPE_PREFERRED) {
            *out = modes[i];
            return 0;
        }
    }
    if(current && current->hdisplay && current->vdisplay) {
        *out = *current;
        return 0;
    }
    for(i = 0; i < count; i++) {
        long area = (long)modes[i].hdisplay * (long)modes[i].vdisplay;
        if(area > best_area) {
            best_area = area;
            best = (int)i;
        }
    }
    if(best < 0) return -1;
    *out = modes[best];
    return 0;
}

/* One pass over the card's connectors. `census` receives one compact token
 * per connector ("28=T16 off m1") so a refusal in the log names exactly what
 * the kernel answered. Returns 0 when a connected connector with a usable
 * mode was found; 1 when the card answered and nothing connected had a mode
 * - the best modeful connector, whatever its status, is then still returned
 * for a forced modeset; -1 when an ioctl failed. */
static int scan_card(int fd, int prefer_w, int prefer_h,
                     struct drm_mode_modeinfo *mode_out,
                     uint32_t *conn_out, uint32_t *enc_out, uint32_t *crtc_out,
                     char *census, size_t census_sz)
{
    struct drm_mode_card_res res;
    struct drm_mode_get_connector conn;
    struct drm_mode_get_encoder enc;
    uint32_t *crtcs = NULL, *connectors = NULL, *encoders = NULL;
    uint32_t i, j;
    uint32_t best_conn = 0, best_enc = 0, best_crtc = 0;
    struct drm_mode_modeinfo best_mode;
    int best_rank = 0;
    int have_connected = 0;
    int rc = 1;

    memset(&best_mode, 0, sizeof(best_mode));
    memset(mode_out, 0, sizeof(*mode_out));
    *conn_out = *enc_out = *crtc_out = 0;
    if(census && census_sz) census[0] = '\0';

    memset(&res, 0, sizeof(res));
    if(ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0) {
        vio_log("drm: GETRESOURCES failed: %s", strerror(errno));
        return -1;
    }
    if(res.count_crtcs == 0 || res.count_connectors == 0) {
        vio_log("drm: card has no CRTC/connector");
        return -1;
    }

    crtcs = calloc(res.count_crtcs, sizeof(*crtcs));
    connectors = calloc(res.count_connectors, sizeof(*connectors));
    encoders = calloc(res.count_encoders ? res.count_encoders : 1,
                      sizeof(*encoders));
    if(!crtcs || !connectors || !encoders) return -1;

    res.crtc_id_ptr = (uint64_t)(uintptr_t)crtcs;
    res.connector_id_ptr = (uint64_t)(uintptr_t)connectors;
    res.encoder_id_ptr = (uint64_t)(uintptr_t)encoders;
    res.fb_id_ptr = 0;
    res.count_fbs = 0;
    if(ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0) {
        vio_log("drm: GETRESOURCES(2) failed: %s", strerror(errno));
        rc = -1;
        goto out;
    }

    for(i = 0; i < res.count_connectors; i++) {
        struct drm_mode_modeinfo *modes = NULL;
        uint32_t *conn_encoders = NULL;
        uint32_t conn_enc = 0, crtc = 0;
        int rank;

        memset(&conn, 0, sizeof(conn));
        conn.connector_id = connectors[i];
        if(ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn) < 0) {
            census_add(census, census_sz, "%u=E? ", connectors[i]);
            continue;
        }

        rank = conn_rank(conn.connector_type);
        census_add(census, census_sz, "%u=T%u %s m%u ", connectors[i],
                   conn.connector_type, conn_state_name(conn.connection),
                   conn.count_modes);

        if(conn.count_modes == 0) continue;

        modes = calloc(conn.count_modes, sizeof(*modes));
        conn_encoders = calloc(conn.count_encoders ? conn.count_encoders : 1,
                               sizeof(*conn_encoders));
        if(!modes || !conn_encoders) { free(modes); free(conn_encoders); continue; }

        /* msm-4.14 copies connector properties whenever count_props is
         * non-zero. The first (counting) ioctl leaves count_props > 0 and
         * props_ptr == NULL, so the SECOND ioctl - the one that actually
         * fetches the mode list - faults with EFAULT. Measured 2026-11-09:
         * census said "28=T16 on m1" (DSI, connected, one mode) for 61
         * scans, yet no mode was ever chosen and the UI stayed dark.
         * Properties are not needed for a modeset: ask for modes only. */
        conn.count_props = 0;
        conn.props_ptr = 0;
        conn.prop_values_ptr = 0;
        conn.modes_ptr = (uint64_t)(uintptr_t)modes;
        conn.encoders_ptr = (uint64_t)(uintptr_t)conn_encoders;
        if(ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn) < 0) {
            vio_log("drm: GETCONNECTOR(%u) mode fetch failed: %s",
                    connectors[i], strerror(errno));
            free(modes);
            free(conn_encoders);
            continue;
        }
        if(conn.count_modes == 0) {
            free(modes);
            free(conn_encoders);
            continue;
        }

        conn_enc = conn.encoder_id ? conn.encoder_id
                                   : (conn.count_encoders ? conn_encoders[0] : 0);
        memset(&enc, 0, sizeof(enc));
        enc.encoder_id = conn_enc;
        if(enc.encoder_id && ioctl(fd, DRM_IOCTL_MODE_GETENCODER, &enc) < 0)
            memset(&enc, 0, sizeof(enc));
        if(enc.crtc_id) crtc = enc.crtc_id;
        if(crtc == 0 && enc.possible_crtcs) {
            for(j = 0; j < res.count_crtcs; j++)
                if(enc.possible_crtcs & (1u << j)) { crtc = crtcs[j]; break; }
        }

        if(conn.connection == VIO_CONN_CONNECTED) {
            struct drm_mode_modeinfo m;

            if(rank >= best_rank &&
               pick_mode(modes, conn.count_modes, NULL, prefer_w, prefer_h,
                         &m) == 0) {
                best_rank = rank;
                best_conn = conn.connector_id;
                best_enc = conn_enc;
                best_crtc = crtc ? crtc : crtcs[0];
                best_mode = m;
                have_connected = 1;
            }
        }
        else if(!have_connected && rank >= best_rank) {
            /* Remember the best modeful connector even when it says
             * disconnected: a DSI panel's mode list comes from the panel
             * driver, not the link state, and the first modeset is often the
             * only thing the link was waiting for. */
            struct drm_mode_modeinfo m;

            if(pick_mode(modes, conn.count_modes, NULL, prefer_w, prefer_h,
                         &m) == 0) {
                best_rank = rank;
                best_conn = conn.connector_id;
                best_enc = conn_enc;
                best_crtc = crtc ? crtc : crtcs[0];
                best_mode = m;
            }
        }
        free(modes);
        free(conn_encoders);
    }

    if(best_conn) {
        *mode_out = best_mode;
        *conn_out = best_conn;
        *enc_out = best_enc;
        *crtc_out = best_crtc;
        rc = have_connected ? 0 : 1;
    }

out:
    free(crtcs);
    free(connectors);
    free(encoders);
    return rc;
}

static int set_crtc(struct vio_drm *drm)
{
    struct drm_mode_crtc setcrtc;
    uint32_t connector = drm->connector_id;

    memset(&setcrtc, 0, sizeof(setcrtc));
    setcrtc.set_connectors_ptr = (uint64_t)(uintptr_t)&connector;
    setcrtc.count_connectors = 1;
    setcrtc.crtc_id = drm->crtc_id;
    setcrtc.fb_id = drm->fb_id;
    setcrtc.mode_valid = 1;
    setcrtc.mode = drm->mode;
    if(ioctl(drm->fd, DRM_IOCTL_MODE_SETCRTC, &setcrtc) < 0) {
        vio_log("drm: SETCRTC failed: %s", strerror(errno));
        return -1;
    }
    return 0;
}

/* Last resort when GETCONNECTOR refuses to hand out a mode list. The
 * bootloader has already modeset the DSI panel - the kernel ring shows
 * dsi_display_bind and splash memory - so GETCRTC on the active CRTC returns
 * a valid mode. Reusing it means SETCRTC only swaps the framebuffer, which is
 * exactly the least risky operation on a trained DSI link. */
static int active_crtc_mode(int fd, uint32_t *crtc_out,
                            struct drm_mode_modeinfo *mode_out,
                            uint32_t *conn_out)
{
    struct drm_mode_card_res res;
    struct drm_mode_crtc cr;
    struct drm_mode_get_connector conn;
    uint32_t *crtcs = NULL, *connectors = NULL, *encoders = NULL;
    struct drm_mode_modeinfo best_mode;
    uint32_t best_crtc = 0, best_conn = 0;
    long best_area = -1;
    int best_rank = -1;
    uint32_t i;

    memset(&res, 0, sizeof(res));
    if(ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0) return -1;
    if(res.count_crtcs == 0 || res.count_connectors == 0) return -1;
    crtcs = calloc(res.count_crtcs, sizeof(*crtcs));
    connectors = calloc(res.count_connectors, sizeof(*connectors));
    encoders = calloc(res.count_encoders ? res.count_encoders : 1,
                      sizeof(*encoders));
    if(!crtcs || !connectors || !encoders) goto fail;
    res.crtc_id_ptr = (uint64_t)(uintptr_t)crtcs;
    res.connector_id_ptr = (uint64_t)(uintptr_t)connectors;
    res.encoder_id_ptr = (uint64_t)(uintptr_t)encoders;
    res.fb_id_ptr = 0;
    res.count_fbs = 0;
    if(ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0) goto fail;

    memset(&best_mode, 0, sizeof(best_mode));
    for(i = 0; i < res.count_crtcs; i++) {
        long area;
        memset(&cr, 0, sizeof(cr));
        cr.crtc_id = crtcs[i];
        if(ioctl(fd, DRM_IOCTL_MODE_GETCRTC, &cr) < 0) continue;
        if(!cr.mode_valid || !cr.mode.hdisplay || !cr.mode.vdisplay) continue;
        area = (long)cr.mode.hdisplay * (long)cr.mode.vdisplay;
        if(area > best_area) {
            best_area = area;
            best_mode = cr.mode;
            best_crtc = crtcs[i];
        }
    }
    if(best_crtc == 0) goto fail;

    for(i = 0; i < res.count_connectors; i++) {
        int rank;
        memset(&conn, 0, sizeof(conn));
        conn.connector_id = connectors[i];
        if(ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn) < 0) continue;
        rank = conn_rank(conn.connector_type);
        if(conn.connection == VIO_CONN_CONNECTED && rank > best_rank) {
            best_rank = rank;
            best_conn = connectors[i];
        }
    }
    if(best_conn == 0) best_conn = connectors[0];

    *crtc_out = best_crtc;
    *mode_out = best_mode;
    *conn_out = best_conn;
    free(crtcs); free(connectors); free(encoders);
    return 0;

fail:
    free(crtcs); free(connectors); free(encoders);
    return -1;
}

int vio_drm_init(struct vio_drm *drm, int prefer_w, int prefer_h)
{
    struct drm_mode_crtc current;
    struct drm_mode_create_dumb dumb;
    struct drm_mode_map_dumb map;
    struct drm_mode_fb_cmd fb;
    char census[1024] = "", last_census[1024] = "";
    struct drm_mode_modeinfo chosen_mode;
    uint32_t chosen_connector = 0, chosen_encoder = 0, chosen_crtc = 0;
    unsigned try;
    int forced = 0;
    int ok = -1;

    memset(drm, 0, sizeof(*drm));
    memset(&chosen_mode, 0, sizeof(chosen_mode));
    drm->fd = -1;

    drm->fd = drm_open_card();
    if(drm->fd < 0) {
        vio_log("drm: no /dev/dri/card0: %s", strerror(errno));
        return -1;
    }

    /* msm-4.14 rejects SETCRTC with EINVAL unless the caller is DRM master.
     * The UMOS board port called SET_MASTER before the commit; keep that here
     * so the modeset is accepted. A refusal is logged only - another client
     * may already own the card, but SETCRTC will then report its own verdict. */
    if(ioctl(drm->fd, DRM_IOCTL_SET_MASTER, 0) == 0)
        vio_log("drm: master of /dev/dri/card0");
    else
        vio_log("drm: SET_MASTER refused (%s) - another client may own the card",
                strerror(errno));

    /* Poll for a connected display, logging every census change, then modeset
     * the best connector that still lists the panel's mode anyway: on a DSI
     * panel the first modeset is often the only thing the link was waiting
     * for. */
    for(try = 0; try < DRM_SCAN_TRIES; try++) {
        struct drm_mode_modeinfo m;
        uint32_t c = 0, e = 0, cr = 0;
        int rc;

        memset(&m, 0, sizeof(m));
        rc = scan_card(drm->fd, prefer_w, prefer_h, &m, &c, &e, &cr,
                       census, sizeof(census));
        if(rc < 0) goto out;
        if(strcmp(census, last_census) != 0) {
            vio_log("drm: census[%u]: %s", try + 1, census);
            snprintf(last_census, sizeof(last_census), "%s", census);
        }
        if(m.hdisplay) {
            chosen_mode = m;
            chosen_connector = c;
            chosen_encoder = e;
            chosen_crtc = cr;
        }
        if(rc == 0) {
            vio_log("drm: connector %u connected, %ux%u, encoder %u crtc %u",
                    chosen_connector, chosen_mode.hdisplay, chosen_mode.vdisplay,
                    chosen_encoder, chosen_crtc);
            break;
        }
        if(try + 1 >= DRM_FORCE_AFTER && chosen_connector) {
            forced = 1;
            vio_log("drm: no connected display after %u scans; forcing modeset "
                    "on connector %u, mode %ux%u",
                    try + 1, chosen_connector,
                    chosen_mode.hdisplay, chosen_mode.vdisplay);
            break;
        }
        vio_sleep_ms(DRM_SCAN_POLL_MS);
    }

    if(chosen_connector == 0 || chosen_mode.hdisplay == 0) {
        vio_log("drm: no display found after %u scans (last census: %s)",
                try + 1, last_census[0] ? last_census : "none");
        if(active_crtc_mode(drm->fd, &chosen_crtc, &chosen_mode,
                            &chosen_connector) == 0) {
            forced = 1;
            vio_log("drm: fallback to active CRTC %u mode %ux%u, connector %u",
                    chosen_crtc, chosen_mode.hdisplay, chosen_mode.vdisplay,
                    chosen_connector);
        } else {
            vio_log("drm: active CRTC fallback failed too");
            goto out;
        }
    }

    drm->connector_id = chosen_connector;
    drm->crtc_id = chosen_crtc;
    drm->mode = chosen_mode;
    drm->width = drm->mode.hdisplay;
    drm->height = drm->mode.vdisplay;
    vio_log("drm: mode %ux%u \"%s\"%s", drm->width, drm->height, drm->mode.name,
            forced ? " (forced)" : "");

    /* Prefer the mode the bootloader already programmed: it is the one the
     * panel was trained for, and SETCRTC then changes nothing but the fb. */
    memset(&current, 0, sizeof(current));
    current.crtc_id = chosen_crtc;
    if(ioctl(drm->fd, DRM_IOCTL_MODE_GETCRTC, &current) == 0 &&
       current.mode_valid && current.mode.hdisplay && current.mode.vdisplay) {
        drm->mode = current.mode;
        vio_log("drm: reusing active mode %ux%u@%u",
                current.mode.hdisplay, current.mode.vdisplay,
                current.mode.vrefresh);
    }

    memset(&dumb, 0, sizeof(dumb));
    dumb.width = (uint32_t)drm->width;
    dumb.height = (uint32_t)drm->height;
    dumb.bpp = 32;
    if(ioctl(drm->fd, DRM_IOCTL_MODE_CREATE_DUMB, &dumb) < 0) {
        vio_log("drm: CREATE_DUMB failed: %s", strerror(errno));
        goto out;
    }
    drm->handle = dumb.handle;
    drm->pitch = dumb.pitch;
    drm->buffer_size = dumb.size;

    memset(&fb, 0, sizeof(fb));
    fb.width = (uint32_t)drm->width;
    fb.height = (uint32_t)drm->height;
    fb.pitch = dumb.pitch;
    fb.bpp = 32;
    fb.depth = 24;
    fb.handle = dumb.handle;
    if(ioctl(drm->fd, DRM_IOCTL_MODE_ADDFB, &fb) < 0) {
        vio_log("drm: ADDFB failed: %s", strerror(errno));
        goto out;
    }
    drm->fb_id = fb.fb_id;

    memset(&map, 0, sizeof(map));
    map.handle = dumb.handle;
    if(ioctl(drm->fd, DRM_IOCTL_MODE_MAP_DUMB, &map) < 0) {
        vio_log("drm: MAP_DUMB failed: %s", strerror(errno));
        goto out;
    }
    drm->map = mmap(NULL, dumb.size, PROT_READ | PROT_WRITE, MAP_SHARED,
                    drm->fd, (off_t)map.offset);
    if(drm->map == MAP_FAILED) {
        drm->map = NULL;
        vio_log("drm: mmap failed: %s", strerror(errno));
        goto out;
    }
    memset(drm->map, 0, dumb.size);
    vio_log("drm: dumb buffer %ux%u pitch %u, %llu bytes",
            drm->width, drm->height, drm->pitch,
            (unsigned long long)drm->buffer_size);

    if(set_crtc(drm) < 0) goto out;

    /* Confirm the CRTC actually took our framebuffer - on a forced modeset
     * this is the difference between "the ioctl was accepted" and "the panel
     * is scanning our pixels". */
    memset(&current, 0, sizeof(current));
    current.crtc_id = drm->crtc_id;
    if(ioctl(drm->fd, DRM_IOCTL_MODE_GETCRTC, &current) == 0 &&
       current.mode_valid && current.fb_id == drm->fb_id) {
        vio_log("drm: verified - crtc %u scans our fb %u at %ux%u",
                drm->crtc_id, drm->fb_id,
                current.mode.hdisplay, current.mode.vdisplay);
    } else {
        vio_log("drm: warn - crtc %u did not confirm fb %u (mode_valid=%d fb=%u)",
                drm->crtc_id, drm->fb_id, current.mode_valid, current.fb_id);
    }

    /* DIRTYFB is how a dumb buffer is announced on msm. It is optional: the
     * CRTC scans the buffer continuously, so a failure only means we skip the
     * hint. Probe it once to know which path we are on. */
    drm->dirty_ok = 1;
    drm->dirty_ok = vio_drm_flush(drm, 0, 0, drm->width - 1, drm->height - 1) == 0 &&
                    drm->dirty_ok;
    vio_log("drm: scanout ready (%s)", drm->dirty_ok ? "DIRTYFB" : "continuous scanout");
    ok = 0;

out:
    if(ok < 0) vio_drm_fini(drm);
    return ok;
}

int vio_drm_flush(struct vio_drm *drm, int x1, int y1, int x2, int y2)
{
    struct drm_clip_rect clip;
    struct drm_mode_fb_dirty_cmd dirty;

    if(drm->fd < 0 || !drm->map) return -1;
    if(x1 < 0) x1 = 0;
    if(y1 < 0) y1 = 0;
    if(x2 >= drm->width) x2 = drm->width - 1;
    if(y2 >= drm->height) y2 = drm->height - 1;
    if(x1 > x2 || y1 > y2) return 0;

    if(!drm->dirty_ok) return 0; /* the CRTC scans the mapped buffer continuously */

    clip.x1 = (unsigned short)x1;
    clip.y1 = (unsigned short)y1;
    clip.x2 = (unsigned short)(x2 + 1);
    clip.y2 = (unsigned short)(y2 + 1);
    memset(&dirty, 0, sizeof(dirty));
    dirty.fb_id = drm->fb_id;
    dirty.num_clips = 1;
    dirty.clips_ptr = (uint64_t)(uintptr_t)&clip;
    if(ioctl(drm->fd, DRM_IOCTL_MODE_DIRTYFB, &dirty) == 0) return 0;

    /* DIRTYFB is only a hint for deferred-IO users; msm scans the dumb buffer
     * every frame, so a partial update is already on the panel. Do not run a
     * full modeset per flush - that would flicker and cost milliseconds. */
    vio_log("drm: DIRTYFB unsupported (%s); relying on continuous scanout",
            strerror(errno));
    drm->dirty_ok = 0;
    return 0;
}

void vio_drm_fini(struct vio_drm *drm)
{
    if(drm->map) {
        munmap(drm->map, (size_t)drm->buffer_size);
        drm->map = NULL;
    }
    if(drm->fd >= 0) {
        if(drm->fb_id) {
            uint32_t fb = drm->fb_id;
            (void)ioctl(drm->fd, DRM_IOCTL_MODE_RMFB, &fb);
        }
        if(drm->handle) {
            struct drm_mode_destroy_dumb destroy;
            memset(&destroy, 0, sizeof(destroy));
            destroy.handle = drm->handle;
            (void)ioctl(drm->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
        }
        close(drm->fd);
        drm->fd = -1;
    }
    drm->fb_id = 0;
    drm->handle = 0;
}
