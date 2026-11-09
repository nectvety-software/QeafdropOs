/* SPDX-License-Identifier: MIT */
/* hal/include/hal_board.h - Board-specific callbacks */

#ifndef UMOS_HAL_BOARD_H
#define UMOS_HAL_BOARD_H

#include <stddef.h>

int board_memory(void **arena, size_t *arena_size);
int board_should_exit(void);
void board_poll(void);
void board_harness(void);

/* 1 when the misc boot command is boot-recovery, i.e. the phone was started
 * from p77 instead of the system boot slot. The recovery GUI opens on that boot. */
int board_boot_recovery(void);

/* 1 when the bootloader control block asks for the fastboot menu. Until the
 * real misc reader is wired back in this stays 0; the fastboot screen is then
 * reachable by name / fastboot_open(). */
int board_boot_fastboot(void);

#endif
