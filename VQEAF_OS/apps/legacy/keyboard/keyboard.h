/* SPDX-License-Identifier: MIT */
/* apps/keyboard/keyboard.h - "Blueprint" soft keyboard.
 *
 * Poster layout: light grid paper, a near-black rounded plate, silver keys
 * with number superscripts, dark modifiers and two orange accents (backspace
 * and spacebar). Drawn procedurally in the 360x780 virtual space, centred on
 * whichever LOGICAL surface the current rotation gives us.
 *
 * The module owns a text buffer (typing is not a privileged action) but only
 * QUEUES rotation intents: display_set_orientation() belongs to the board.
 */

#ifndef UMOS_KEYBOARD_H
#define UMOS_KEYBOARD_H

#include <stdint.h>

#define KEYBOARD_EV_TOUCH 1
#define KEYBOARD_EV_BACK  2

enum keyboard_action {
    KEYBOARD_ACT_NONE = 0,
    KEYBOARD_ACT_ROTATE     /* arg: display_orientation, or -2 for auto */
};

void keyboard_init(void);
void keyboard_open(void);
void keyboard_close(void);
int  keyboard_visible(void);
void keyboard_draw(void);
int  keyboard_event(uint32_t type, uint32_t d1, uint32_t d2);

enum keyboard_action keyboard_take_action(int *arg);
const char *keyboard_text(void);
int  keyboard_key_count(void);
/* 0 = letters, 1 = symbols. Shift state is separate. */
int  keyboard_symbols(void);
int  keyboard_shift(void);
/* Layout origin, so a host gate can hit-test the centred landscape plate. */
void keyboard_origin(int *ox, int *oy);

#endif
