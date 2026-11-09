/* SPDX-License-Identifier: MIT */
/* platform/violet/lv_conf.h - Homo OS LVGL config for the violet panel.
 *
 * Derived from the 800x480 Linux board config. The violet kernel has no
 * fbdev and no evdev driver in LVGL here, so both LVGL drivers are disabled:
 * platform/violet/drm_kms.c and violet_input.c own the hardware instead.
 */
#include "../../config/lv_conf_common.h"

#undef LV_MEM_SIZE
/* PNG cache, decode scratch, app snapshots and multiple retained app views
 * coexist. Keep headroom for a cold full-screen wallpaper decode on return. */
#define LV_MEM_SIZE (16U * 1024U * 1024U)

#undef LV_DEF_REFR_PERIOD
#define LV_DEF_REFR_PERIOD 16

#undef LV_FONT_MONTSERRAT_20
#define LV_FONT_MONTSERRAT_20 1
#undef LV_FONT_MONTSERRAT_32
#define LV_FONT_MONTSERRAT_32 1
#undef LV_FONT_MONTSERRAT_12
#define LV_FONT_MONTSERRAT_12 1

#undef LV_FONT_SOURCE_HAN_SANS_SC_14_CJK
#define LV_FONT_SOURCE_HAN_SANS_SC_14_CJK 1

#undef LV_USE_FS_STDIO
#define LV_USE_FS_STDIO 1
#define LV_FS_STDIO_LETTER 'A'
#define LV_FS_STDIO_PATH ""
#define LV_FS_STDIO_CACHE_SIZE 0

#undef LV_USE_LODEPNG
#define LV_USE_LODEPNG 1

#undef LV_USE_TINY_TTF
#define LV_USE_TINY_TTF 1
#undef LV_TINY_TTF_FILE_SUPPORT
#define LV_TINY_TTF_FILE_SUPPORT 1

#undef LV_USE_SDL
#define LV_USE_SDL 0

#undef LV_USE_LINUX_FBDEV
#define LV_USE_LINUX_FBDEV 0

#undef LV_USE_LINUX_DRM
#define LV_USE_LINUX_DRM 0

#undef LV_USE_EVDEV
#define LV_USE_EVDEV 0
