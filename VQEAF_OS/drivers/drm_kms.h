/* SPDX-License-Identifier: MIT */
/* platform/violet/drm_kms.h - minimal DRM/KMS scanout backend (no libdrm). */
#ifndef HOMO_DRM_KMS_H
#define HOMO_DRM_KMS_H

#include <stdint.h>

#include <drm/drm_mode.h>

struct vio_drm {
    int fd;
    int width;
    int height;
    uint32_t pitch;
    uint64_t buffer_size;
    uint32_t fb_id;
    uint32_t handle;
    uint32_t crtc_id;
    uint32_t connector_id;
    uint8_t *map;
    int dirty_ok;
    struct drm_mode_modeinfo mode;
};

/* Opens /dev/dri/card0, picks the connected panel, allocates an XRGB8888
 * dumb buffer and programs the CRTC. Returns 0 on success. */
int vio_drm_init(struct vio_drm *drm, int prefer_w, int prefer_h);

/* Presents the given rectangle of the mapped buffer. */
int vio_drm_flush(struct vio_drm *drm, int x1, int y1, int x2, int y2);

void vio_drm_fini(struct vio_drm *drm);

#endif /* HOMO_DRM_KMS_H */
