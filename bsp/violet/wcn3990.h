/* SPDX-License-Identifier: MIT */
/* bsp/violet/wcn3990.h - seams only the host gate uses on wcn3990.c.
 *
 * The device never calls these: it reads /sys and talks nl80211 for real.
 * They exist so tests/wifi_gui_test.c can compile the same file the phone
 * links and still measure the parsers against captured bytes and a fixture
 * tree of plain files.
 */

#ifndef UMOS_WCN3990_H
#define UMOS_WCN3990_H

#include "hal/include/wifi.h"

/* Read sysfs under `root` instead of /sys (fixture tree). NULL restores
 * the real root. */
void wcn_set_sysfs_root(const char *root);

/* Feed the scan parser a captured NL80211 dump instead of running a live
 * scan. len 0 clears it and wifi_scan goes back to the real transport. */
void wcn_set_scan_dump(const unsigned char *blob, int len);

/* Host-gate only: pretend an association happened, so the screens downstream
 * of wifi_link_info() can be rendered and measured. On the phone nothing
 * calls this, and an association needs a supplicant that is not bound yet
 * (see wifi_connect). */
void wcn_forge_connected(const char *ssid, const struct wifi_link_info *li);

#endif
