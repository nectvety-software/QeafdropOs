/* SPDX-License-Identifier: MIT */
/* hal/include/wifi.h - WiFi radio interface (WCN3990) */

#ifndef UMOS_WIFI_H
#define UMOS_WIFI_H

#include <stdint.h>
#include <stddef.h>

#define WIFI_MAX_SSID 32
#define WIFI_MAX_BSSID 6
#define WIFI_MAX_SCAN_RESULTS 16
#define WIFI_MAX_KEY 64

enum wifi_status {
    WIFI_STATUS_OFF,
    WIFI_STATUS_ON,
    WIFI_STATUS_SCANNING,
    WIFI_STATUS_CONNECTING,
    WIFI_STATUS_CONNECTED,
    WIFI_STATUS_FAILED
};

enum wifi_auth {
    WIFI_AUTH_OPEN,
    WIFI_AUTH_WPA_PSK,
    WIFI_AUTH_WPA2_PSK,
    WIFI_AUTH_WPA3_SAE
};

struct wifi_scan_result {
    uint8_t bssid[WIFI_MAX_BSSID];
    char ssid[WIFI_MAX_SSID + 1];
    int8_t rssi;
    uint8_t channel;
    enum wifi_auth auth;
};

struct wifi_config {
    char ssid[WIFI_MAX_SSID + 1];
    char key[WIFI_MAX_KEY + 1];
    enum wifi_auth auth;
};

/* What the link itself reports once associated. Every figure the details
 * screen prints comes from here; a board that cannot answer returns < 0 and
 * the screen says so instead of inventing a number. */
struct wifi_link_info {
    int rssi_dbm;
    int link_mbps;
    int freq_mhz;
    int channel;
    enum wifi_auth auth;
    char ipv4[16];
    char gateway[16];
    char netmask[16];
    char dns[32];
};

int wifi_init(void);
int wifi_on(void);
int wifi_off(void);
enum wifi_status wifi_status(void);
int wifi_scan(struct wifi_scan_result *results, int max_results);
int wifi_connect(const struct wifi_config *cfg);
int wifi_disconnect(void);
int wifi_is_connected(void);
int wifi_link_info(struct wifi_link_info *out);
/* SSID of the association, or "" while not connected. Borrowed string. */
const char *wifi_current_ssid(void);

#endif
