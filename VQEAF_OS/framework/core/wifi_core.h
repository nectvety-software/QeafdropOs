#ifndef HACHIMI_WIFI_CORE_H
#define HACHIMI_WIFI_CORE_H

#include <stdbool.h>

/* Violet WCN3990 wifi core (Linux 4.14 vendor kernel).
 * Honest probe: wlan+cfg80211 modules are loaded but wlan0 appears only when
 * the WCN firmware from modem (p82) + persist (p74) is staged in
 * /lib/firmware/wlan/qca_cld. This core does not fake success.
 */

bool wifi_core_present(void);
bool wifi_core_fw_ready(void);
const char *wifi_core_state(void);

#endif
