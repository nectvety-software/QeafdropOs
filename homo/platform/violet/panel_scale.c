/* SPDX-License-Identifier: MIT */
/* platform/violet/panel_scale.c - integer-only scale, letterbox and rotate.
 *
 * A 800x480 desktop on a 1080x2340 portrait panel: fit by the shorter axis
 * (2.25x -> 1800x1080), centre it, then rotate the 2340x1080 logical frame
 * into the panel's native portrait orientation. Everything is Q16 fixed point
 * so the host test and the phone compute the same pixel from the same input.
 */
#include "panel_scale.h"

#include <stddef.h>

static void build_maps(vio_panel_t *p)
{
    int logical_w, logical_h, sx, sy, i;

    if(p->rotation == 90 || p->rotation == 270) {
        logical_w = p->panel_h;
        logical_h = p->panel_w;
    }
    else {
        logical_w = p->panel_w;
        logical_h = p->panel_h;
    }
    if(logical_w > VIO_PANEL_MAP_MAX) logical_w = VIO_PANEL_MAP_MAX;
    if(logical_h > VIO_PANEL_MAP_MAX) logical_h = VIO_PANEL_MAP_MAX;
    p->lw = logical_w;
    p->lh = logical_h;

    sx = (logical_w << 16) / p->ui_w;
    sy = (logical_h << 16) / p->ui_h;
    p->scale_q16 = sx < sy ? sx : sy;
    /* Round the scaled size so an exact fit (1080/800 = 1.35) does not lose
     * the last row or column to truncation. */
    p->out_w = (int)((((int64_t)p->ui_w * p->scale_q16) + 32768) >> 16);
    p->out_h = (int)((((int64_t)p->ui_h * p->scale_q16) + 32768) >> 16);
    if(p->out_w > logical_w) p->out_w = logical_w;
    if(p->out_h > logical_h) p->out_h = logical_h;
    p->offset_x = (logical_w - p->out_w) / 2;
    p->offset_y = (logical_h - p->out_h) / 2;

    for(i = 0; i < logical_w; i++) {
        int rel = i - p->offset_x;
        p->map_lx[i] = (rel < 0 || rel >= p->out_w)
                       ? -1
                       : (int16_t)(((int64_t)rel << 16) / p->scale_q16);
    }
    for(i = 0; i < logical_h; i++) {
        int rel = i - p->offset_y;
        p->map_ly[i] = (rel < 0 || rel >= p->out_h)
                       ? -1
                       : (int16_t)(((int64_t)rel << 16) / p->scale_q16);
    }
}

void vio_panel_init(vio_panel_t *panel, int ui_w, int ui_h,
                    int panel_w, int panel_h, int rotation)
{
    panel->ui_w = ui_w;
    panel->ui_h = ui_h;
    panel->panel_w = panel_w;
    panel->panel_h = panel_h;
    panel->stride = panel_w;
    panel->rotation = 0;
    vio_panel_set_rotation(panel, rotation);
}

void vio_panel_set_stride(vio_panel_t *panel, int stride)
{
    panel->stride = (stride >= panel->panel_w) ? stride : panel->panel_w;
}

void vio_panel_set_ui_size(vio_panel_t *panel, int ui_w, int ui_h)
{
    if(ui_w <= 0 || ui_h <= 0) return;
    panel->ui_w = ui_w;
    panel->ui_h = ui_h;
    build_maps(panel);
}

void vio_panel_set_rotation(vio_panel_t *panel, int rotation)
{
    rotation %= 360;
    if(rotation < 0) rotation += 360;
    if(rotation != 90 && rotation != 180 && rotation != 270) rotation = 0;
    panel->rotation = rotation;
    build_maps(panel);
}

vio_rect_t vio_panel_full_rect(const vio_panel_t *panel)
{
    vio_rect_t rect;
    rect.x1 = 0;
    rect.y1 = 0;
    rect.x2 = panel->panel_w - 1;
    rect.y2 = panel->panel_h - 1;
    return rect;
}

static inline uint32_t expand_rgb565(uint16_t value)
{
    unsigned r = (value >> 11) & 0x1F;
    unsigned g = (value >> 5) & 0x3F;
    unsigned b = value & 0x1F;
    r = (r << 3) | (r >> 2);
    g = (g << 2) | (g >> 4);
    b = (b << 3) | (b >> 2);
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

static inline void plot(const vio_panel_t *p, const uint16_t *ui,
                        uint32_t *scanout, int px, int py, int lx, int ly)
{
    uint32_t color = 0xFF000000u;
    if(lx >= 0 && lx < p->lw && ly >= 0 && ly < p->lh) {
        int ux = p->map_lx[lx];
        int uy = p->map_ly[ly];
        if(ux >= 0 && uy >= 0 && ux < p->ui_w && uy < p->ui_h)
            color = expand_rgb565(ui[(size_t)uy * (size_t)p->ui_w + (size_t)ux]);
    }
    scanout[(size_t)py * (size_t)p->stride + (size_t)px] = color;
}

void vio_panel_render(const vio_panel_t *p, const uint16_t *ui,
                      uint32_t *scanout, vio_rect_t rect)
{
    int px, py;

    if(rect.x1 < 0) rect.x1 = 0;
    if(rect.y1 < 0) rect.y1 = 0;
    if(rect.x2 >= p->panel_w) rect.x2 = p->panel_w - 1;
    if(rect.y2 >= p->panel_h) rect.y2 = p->panel_h - 1;
    if(rect.x1 > rect.x2 || rect.y1 > rect.y2) return;

    switch(p->rotation) {
    case 90:
        /* physical px = lh-1-ly, physical py = lx */
        for(py = rect.y1; py <= rect.y2; py++)
            for(px = rect.x1; px <= rect.x2; px++)
                plot(p, ui, scanout, px, py, py, p->lh - 1 - px);
        break;
    case 180:
        for(py = rect.y1; py <= rect.y2; py++)
            for(px = rect.x1; px <= rect.x2; px++)
                plot(p, ui, scanout, px, py, p->lw - 1 - px, p->lh - 1 - py);
        break;
    case 270:
        /* physical px = ly, physical py = lw-1-lx */
        for(py = rect.y1; py <= rect.y2; py++)
            for(px = rect.x1; px <= rect.x2; px++)
                plot(p, ui, scanout, px, py, p->lw - 1 - py, px);
        break;
    default:
        for(py = rect.y1; py <= rect.y2; py++)
            for(px = rect.x1; px <= rect.x2; px++)
                plot(p, ui, scanout, px, py, px, py);
        break;
    }
}

int vio_panel_touch_map(const vio_panel_t *p, int raw_x, int raw_y,
                        int raw_w, int raw_h, int *ui_x, int *ui_y)
{    int px, py, lx, ly, ux, uy;

    if(raw_w <= 0) raw_w = p->panel_w;
    if(raw_h <= 0) raw_h = p->panel_h;
    if(raw_x < 0) raw_x = 0;
    if(raw_y < 0) raw_y = 0;
    if(raw_x >= raw_w) raw_x = raw_w - 1;
    if(raw_y >= raw_h) raw_y = raw_h - 1;

    px = (int)((int64_t)raw_x * p->panel_w / raw_w);
    py = (int)((int64_t)raw_y * p->panel_h / raw_h);
    if(px >= p->panel_w) px = p->panel_w - 1;
    if(py >= p->panel_h) py = p->panel_h - 1;

    switch(p->rotation) {
    case 90:  lx = py;                 ly = p->lh - 1 - px; break;
    case 180: lx = p->lw - 1 - px;     ly = p->lh - 1 - py; break;
    case 270: lx = p->lw - 1 - py;     ly = px;             break;
    default:  lx = px;                 ly = py;             break;
    }
    if(lx < 0 || lx >= p->lw || ly < 0 || ly >= p->lh) return -1;
    ux = p->map_lx[lx];
    uy = p->map_ly[ly];
    if(ux < 0 || uy < 0 || ux >= p->ui_w || uy >= p->ui_h) return -1;
    if(ui_x) *ui_x = ux;
    if(ui_y) *ui_y = uy;
    return 0;
}

vio_rect_t vio_panel_map_ui_rect(const vio_panel_t *p, vio_rect_t ui)
{
    vio_rect_t out;
    int lx1, lx2, ly1, ly2;

    if(ui.x1 < 0) ui.x1 = 0;
    if(ui.y1 < 0) ui.y1 = 0;
    if(ui.x2 >= p->ui_w) ui.x2 = p->ui_w - 1;
    if(ui.y2 >= p->ui_h) ui.y2 = p->ui_h - 1;

    /* The upper edge is a ceiling: the inverse map lets the column after
     * floor((x2+1)*scale) still sample x2, so a floor here would leave a
     * one-pixel seam between adjacent LVGL areas. Clamping below keeps the
     * result inside the scaled rectangle. */
    lx1 = p->offset_x + (int)(((int64_t)ui.x1 * p->scale_q16) >> 16);
    lx2 = p->offset_x +
          (int)((((int64_t)(ui.x2 + 1) * p->scale_q16) + 65535) >> 16) - 1;
    ly1 = p->offset_y + (int)(((int64_t)ui.y1 * p->scale_q16) >> 16);
    ly2 = p->offset_y +
          (int)((((int64_t)(ui.y2 + 1) * p->scale_q16) + 65535) >> 16) - 1;

    if(lx1 < p->offset_x) lx1 = p->offset_x;
    if(ly1 < p->offset_y) ly1 = p->offset_y;
    if(lx2 >= p->offset_x + p->out_w) lx2 = p->offset_x + p->out_w - 1;
    if(ly2 >= p->offset_y + p->out_h) ly2 = p->offset_y + p->out_h - 1;
    if(lx1 > lx2 || ly1 > ly2) {
        out.x1 = out.y1 = 0;
        out.x2 = out.y2 = -1;
        return out;
    }

    switch(p->rotation) {
    case 90:
        out.x1 = p->lh - 1 - ly2;
        out.x2 = p->lh - 1 - ly1;
        out.y1 = lx1;
        out.y2 = lx2;
        break;
    case 180:
        out.x1 = p->lw - 1 - lx2;
        out.x2 = p->lw - 1 - lx1;
        out.y1 = p->lh - 1 - ly2;
        out.y2 = p->lh - 1 - ly1;
        break;
    case 270:
        out.x1 = ly1;
        out.x2 = ly2;
        out.y1 = p->lw - 1 - lx2;
        out.y2 = p->lw - 1 - lx1;
        break;
    default:
        out.x1 = lx1;
        out.x2 = lx2;
        out.y1 = ly1;
        out.y2 = ly2;
        break;
    }
    return out;
}
