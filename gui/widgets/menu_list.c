/* SPDX-License-Identifier: MIT */
/* gui/widgets/menu_list.c - Menu/list widget */

#include "menu_list.h"
#include "gfx.h"
#include "theme.h"
#include <string.h>

#define ITEM_HEIGHT 32

void menu_list_init(struct menu_list *ml, struct font *f,
                    int16_t x, int16_t y, uint16_t w, uint16_t h) {
    ml->font = f;
    ml->count = 0;
    ml->selected = 0;
    ml->x = x;
    ml->y = y;
    ml->w = w;
    ml->h = h;
    ml->user_arg = NULL;
    memset(ml->items, 0, sizeof(ml->items));
}

int menu_list_add(struct menu_list *ml, const char *label,
                  void (*on_select)(struct menu_item *item, void *arg)) {
    if (ml->count >= MENU_MAX_ITEMS) return -1;
    struct menu_item *it = &ml->items[ml->count];
    it->label = label;
    it->on_select = on_select;
    it->checked = 0;
    it->disabled = 0;
    ml->count++;
    return ml->count - 1;
}

void menu_list_draw(struct menu_list *ml) {
    struct umos_rect bg = {ml->x, ml->y, ml->w, ml->h};
    gfx_fill_rect(&bg, g_theme.surface);
    gfx_draw_rect(&bg, g_theme.border);
    if (!ml->font) return;
    for (int i = 0; i < ml->count; i++) {
        int16_t ry = ml->y + 4 + (int16_t)i * ITEM_HEIGHT;
        struct umos_rect item = {ml->x + 4, ry, ml->w - 8, ITEM_HEIGHT - 4};
        struct umos_color textcol = g_theme.text;
        if (i == ml->selected) {
            gfx_fill_rect(&item, g_theme.primary);
            textcol = g_theme.on_primary;
        } else {
            gfx_fill_rect(&item, g_theme.surface);
        }
        if (ml->items[i].disabled) textcol = g_theme.secondary;
        font_draw_string(ml->font, ml->items[i].label, item.x + 8, item.y + 8, textcol);
        /* Checkbox indicator */
        if (ml->items[i].checked) {
            font_draw_string(ml->font, "X", ml->x + (int16_t)ml->w - 24, item.y + 8, textcol);
        }
    }
}

int menu_list_event(struct menu_list *ml, uint32_t type, uint32_t d1, uint32_t d2) {
    (void)d2;
    if (ml->count == 0) return 0;
    /* type 1 = up, 2 = down, 3 = activate, 4 = click at index in d1 */
    if (type == 1) { /* up */
        ml->selected = (ml->selected - 1 + ml->count) % ml->count;
        return 1;
    } else if (type == 2) { /* down */
        ml->selected = (ml->selected + 1) % ml->count;
        return 1;
    } else if (type == 3) { /* activate */
        menu_list_activate(ml);
        return 1;
    } else if (type == 4) { /* click at index */
        if (d1 < (uint32_t)ml->count) {
            ml->selected = (int)d1;
            return 1;
        }
    }
    return 0;
}

void menu_list_select_next(struct menu_list *ml) {
    if (ml->count) ml->selected = (ml->selected + 1) % ml->count;
}

void menu_list_select_prev(struct menu_list *ml) {
    if (ml->count) ml->selected = (ml->selected - 1 + ml->count) % ml->count;
}

void menu_list_activate(struct menu_list *ml) {
    if (ml->selected < 0 || ml->selected >= ml->count) return;
    struct menu_item *it = &ml->items[ml->selected];
    if (it->disabled) return;
    if (it->on_select) it->on_select(it, ml->user_arg);
}
