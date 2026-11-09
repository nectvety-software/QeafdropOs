/* SPDX-License-Identifier: MIT */
/* bsp/violet/bluetooth_hci.h - host-gate seams for bluetooth_hci.c.
 *
 * The phone never calls these. They let tests/bt_gui_test.c compile the same
 * object the aarch64 link uses and measure the parsers against captured bytes
 * and a fixture /sys tree of ordinary files.
 */

#ifndef UMOS_BLUETOOTH_HCI_H
#define UMOS_BLUETOOTH_HCI_H

#include "hal/include/bluetooth.h"

/* Wire format of a captured inquiry result. Kept here so the gate assembles
 * the blob with the same constants the parser uses. */
#define BT_DUMP_MAGIC  "QBT1"
#define BT_DUMP_HDR    8       /* magic[4] + rec_size u16 + count u16 */
#define BT_REC_SIZE    64
#define BT_DUMP_CAP    4096

/* Read sysfs under `root` instead of /sys (fixture tree). NULL restores it. */
void bt_set_sysfs_root(const char *root);

/* Feed the parser a captured inquiry dump instead of running a live one.
 * len 0 clears it and bt_scan goes back to the real transport. */
void bt_set_scan_dump(const unsigned char *blob, int len);

/* Host-gate only: pretend a link came up so the details page downstream of
 * bt_link_info() can be rendered and measured. */
void bt_forge_connected(const char *name, const struct bt_link_info *li);

#endif
