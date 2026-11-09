/* SPDX-License-Identifier: MIT */
/* hal/include/bluetooth.h - Bluetooth radio interface (violet HCI adapter).
 *
 * Mirrors hal/include/wifi.h deliberately: one seam for the radio, one for
 * the link, and a scan that reports what the adapter actually returned. A
 * board with no hci0 gets -1 and an empty list, never an invented roster.
 */

#ifndef UMOS_BLUETOOTH_H
#define UMOS_BLUETOOTH_H

#include <stdint.h>
#include <stddef.h>

#define BT_ADDR_LEN      6
#define BT_MAX_NAME      30
#define BT_MAX_DEVICES   16
#define BT_PROFILE_COUNT 5

#define BT_BATTERY_UNKNOWN 0xFF

enum bt_status {
    BT_STATUS_OFF = 0,
    BT_STATUS_ON,
    BT_STATUS_SCANNING,
    BT_STATUS_CONNECTED,
    BT_STATUS_FAILED
};

enum bt_device_type {
    BT_DEV_HEADSET = 0,
    BT_DEV_WATCH,
    BT_DEV_CAR,
    BT_DEV_SPEAKER,
    BT_DEV_PHONE,
    BT_DEV_KEYBOARD,
    BT_DEV_UNKNOWN
};

enum bt_codec {
    BT_CODEC_NONE = 0,
    BT_CODEC_SBC,
    BT_CODEC_AAC,
    BT_CODEC_LDAC,
    BT_CODEC_APTX
};

/* Profile switches shown on the device details page. */
enum bt_profile_bit {
    BT_PROFILE_PHONE_CALLS = 1u << 0,
    BT_PROFILE_MEDIA_AUDIO = 1u << 1,
    BT_PROFILE_CONTACTS    = 1u << 2,
    BT_PROFILE_INPUT       = 1u << 3,
    BT_PROFILE_SHARE_DATA  = 1u << 4
};

struct bt_device {
    uint8_t addr[BT_ADDR_LEN];
    char    name[BT_MAX_NAME + 1];
    int8_t  rssi;
    uint8_t battery;         /* 0..100 or BT_BATTERY_UNKNOWN */
    uint8_t type;            /* enum bt_device_type */
    uint8_t codec;           /* enum bt_codec */
    uint8_t paired;
    uint8_t connected;
    uint8_t profiles;        /* bitmask of enum bt_profile_bit */
};

/* What the active link reports. A board that cannot answer returns < 0 and
 * the screen prints "no answer" instead of a datasheet number. */
struct bt_link_info {
    int rssi_dbm;
    int freq_mhz;            /* 2400 for the BR/EDR band */
    int battery;             /* -1 when the peer reports none */
    int le;                  /* 0 = Classic, 1 = LE */
    enum bt_codec codec;
    char mac[18];
    char ipv4[16];
    char gateway[16];
    char netmask[16];
    char dns[32];
};

int bt_init(void);
int bt_on(void);
int bt_off(void);
enum bt_status bt_status(void);

/* Fills up to max rows; returns the count, or -1 when the adapter cannot
 * answer. 0 means the adapter answered and found nothing. */
int bt_scan(struct bt_device *out, int max);

/* Pairing / connecting belongs to a stack that is not bound here yet, so
 * these return -1 rather than pretending a bond exists. */
int bt_pair(const uint8_t addr[BT_ADDR_LEN]);
int bt_unpair(const uint8_t addr[BT_ADDR_LEN]);
int bt_connect(const uint8_t addr[BT_ADDR_LEN]);
int bt_disconnect(void);

int bt_is_connected(void);
int bt_link_info(struct bt_link_info *out);
/* Name of the active peer, or "" while disconnected. Borrowed string. */
const char *bt_current_name(void);

/* Session profile switches, shared by every details page. */
int  bt_profile(int index);
void bt_set_profile(int index, int on);

#endif
