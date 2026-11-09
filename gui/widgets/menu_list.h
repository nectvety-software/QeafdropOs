/* SPDX-License-Identifier: MIT */
/* gui/widgets/menu_list.h - Menu/list widget for recovery and settings */

#ifndef UMOS_MENU_LIST_H
#define UMOS_MENU_LIST_H

#include "gfx.h"
#include "font.h"

#define MENU_MAX_ITEMS 16

struct menu_item {
    const char *label;
    void (*on_select)(struct menu_item *item, void *arg);
    int checked; /* for checkbox style */
    int disabled;
};

struct menu_list {
    struct font *font;
    struct menu_item items[MENU_MAX_ITEMS];
    int count;
    int selected;
    int16_t x, y;
    uint16_t w, h;
    void *user_arg;
};

void menu_list_init(struct menu_list *ml, struct font *f,
                    int16_t x, int16_t y, uint16_t w, uint16_t h);
int menu_list_add(struct menu_list *ml, const char *label,
                  void (*on_select)(struct menu_item *item, void *arg));
void menu_list_draw(struct menu_list *ml);
int menu_list_event(struct menu_list *ml, uint32_t type, uint32_t d1, uint32_t d2);
void menu_list_select_next(struct menu_list *ml);
void menu_list_select_prev(struct menu_list *ml);
void menu_list_activate(struct menu_list *ml);

#endif
