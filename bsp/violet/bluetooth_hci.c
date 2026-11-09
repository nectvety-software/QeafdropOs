/* SPDX-License-Identifier: MIT */
/* bsp/violet/bluetooth_hci.c - HCI adapter, reading the radio instead of
 * reciting a roster.
 *
 * There is no stored pairing list here. Every device on the settings screen
 * comes from an inquiry dump parsed below, or from the real adapter through
 * sysfs. On a boot where the UART/HCI chip never came up, /sys/class/bluetooth
 * has no hci0; the seam then answers -1 and the screen says "Radio not ready".
 *
 * The dump is a captured inquiry reply in a compact fixed record (see
 * bluetooth_hci.h). It is deliberately NOT a netlink socket: the host gate has
 * no HCI transport, so it feeds captured bytes through the very parser the
 * phone links. Live path stays honest: presence of hci0 is checked, but with
 * no inquiry socket bound bt_scan refuses instead of inventing peers.
 */

#include "hal/include/bluetooth.h"
#include "bsp/violet/bluetooth_hci.h"
#include "core/sysio.h"
#include "core/log.h"

#include <stdint.h>
#include <stddef.h>

/* record offsets inside BT_REC_SIZE */
#define R_ADDR   0
#define R_NAME   6
#define R_RSSI   36
#define R_BATT   37
#define R_TYPE   38
#define R_CODEC  39
#define R_PAIRED 40
#define R_CONN   41
#define R_PROF   42

static char g_root[96] = "/sys";
static int g_master;
static int g_assoc;
static enum bt_status g_status = BT_STATUS_OFF;
static struct bt_link_info g_link;
static char g_name[BT_MAX_NAME + 1];
static uint8_t g_profiles = 0x1F;   /* all five profiles on by default */

static unsigned char g_dump[BT_DUMP_CAP];
static int g_dump_len;

/* ---- local string / integer helpers (no libc on the device link) ---------- */

static int streq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static void cpy(char *d, int cap, const char *s) {
    int i = 0;
    if (!s) s = "";
    for (; i + 1 < cap && s[i]; i++) d[i] = s[i];
    d[i] = '\0';
}

static void cat_to(char *d, int cap, const char *s) {
    int n = 0;
    while (n + 1 < cap && d[n]) n++;
    while (*s && n + 1 < cap) d[n++] = *s++;
    d[n] = '\0';
}

static void syspath(char *buf, int cap, const char *sub) {
    cpy(buf, cap, g_root);
    cat_to(buf, cap, sub);
}

static void trim(char *s) {
    int n = 0;
    while (s[n] && s[n] != '\n' && s[n] != '\r') n++;
    s[n] = '\0';
}

static uint16_t le16(const unsigned char *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

/* ---- the parser: what the gate actually measures --------------------------- */

static void zero_dev(struct bt_device *d) {
    unsigned char *b = (unsigned char *)d;
    for (size_t i = 0; i < sizeof(*d); i++) b[i] = 0;
    d->rssi = -120;
    d->battery = BT_BATTERY_UNKNOWN;
    d->type = BT_DEV_UNKNOWN;
    d->codec = BT_CODEC_NONE;
}

static int parse_dump(const unsigned char *p, int len,
                      struct bt_device *out, int max) {
    int n = 0;
    unsigned count;

    if (!p || len < BT_DUMP_HDR || !out || max <= 0) return 0;
    if (p[0] != 'Q' || p[1] != 'B' || p[2] != 'T' || p[3] != '1') return 0;
    if (le16(p + 4) != BT_REC_SIZE) return 0;
    count = le16(p + 6);

    for (unsigned i = 0; i < count && n < max; i++) {
        int off = BT_DUMP_HDR + (int)i * BT_REC_SIZE;
        const unsigned char *r;
        struct bt_device *d;
        int name_n;

        if (off + BT_REC_SIZE > len) break;   /* truncated capture stops here */
        r = p + off;
        d = &out[n];
        zero_dev(d);
        for (int j = 0; j < BT_ADDR_LEN; j++) d->addr[j] = r[R_ADDR + j];
        for (name_n = 0; name_n < BT_MAX_NAME && r[R_NAME + name_n]; name_n++)
            d->name[name_n] = (char)r[R_NAME + name_n];
        d->name[name_n] = '\0';
        d->rssi     = (int8_t)r[R_RSSI];
        d->battery  = r[R_BATT];
        d->type     = r[R_TYPE] > BT_DEV_UNKNOWN ? BT_DEV_UNKNOWN : r[R_TYPE];
        d->codec    = r[R_CODEC] > BT_CODEC_APTX ? BT_CODEC_NONE : r[R_CODEC];
        d->paired   = r[R_PAIRED] ? 1 : 0;
        d->connected = r[R_CONN] ? 1 : 0;
        d->profiles = r[R_PROF];
        n++;
    }

    /* Strongest first; connected peers float to the top of the list. */
    for (int i = 1; i < n; i++) {
        struct bt_device tmp = out[i];
        int k = i - 1;
        while (k >= 0 && (out[k].connected < tmp.connected ||
                          (out[k].connected == tmp.connected &&
                           out[k].rssi < tmp.rssi))) {
            out[k + 1] = out[k];
            k--;
        }
        out[k + 1] = tmp;
    }
    return n;
}

/* ---- live adapter (answers on the phone, refuses on the host) -------------- */

static int adapter_present(void) {
    char p[192];
    char v[32];
    syspath(p, (int)sizeof(p), "/class/bluetooth/hci0/address");
    if (sysio_read_file(p, v, (int)sizeof(v)) < 0) return 0;
    trim(v);
    return v[0] != '\0';
}

static int rfkill_bt_block(int block) {
    char p[192];
    char t[32];

    for (int i = 0; i < 16; i++) {
        char num[8];
        if (i >= 10) {
            num[0] = (char)('0' + i / 10);
            num[1] = (char)('0' + i % 10);
            num[2] = '\0';
        } else {
            num[0] = (char)('0' + i);
            num[1] = '\0';
        }
        syspath(p, (int)sizeof(p), "/class/rfkill/rfkill");
        cat_to(p, (int)sizeof(p), num);
        cat_to(p, (int)sizeof(p), "/type");
        if (sysio_read_file(p, t, (int)sizeof(t)) < 0) continue;
        trim(t);
        if (!streq(t, "bluetooth")) continue;

        int m = 0;
        while (p[m]) m++;
        m -= 5;                     /* strlen("/type") */
        if (m < 0) m = 0;
        p[m] = '\0';
        cat_to(p, (int)sizeof(p), "/soft");
        if (sysio_write_str(p, block ? "1\n" : "0\n") < 0) return -1;
        return 0;
    }
    return -1;
}

/* ---- HAL seam -------------------------------------------------------------- */

int bt_init(void) {
    g_master = 0;
    g_assoc = 0;
    g_dump_len = 0;
    g_name[0] = '\0';
    g_status = BT_STATUS_OFF;
    g_profiles = 0x1F;
    umos_logf("bluetooth: HCI seam bound to the real adapter\n");
    return 0;
}

int bt_on(void) {
    g_master = 1;
    g_status = BT_STATUS_ON;
    int rk = rfkill_bt_block(0);
    umos_logf("bluetooth: on, rfkill %s\n", rk == 0 ? "unblocked" : "absent");
    return 0;
}

int bt_off(void) {
    g_master = 0;
    g_assoc = 0;
    g_name[0] = '\0';
    g_status = BT_STATUS_OFF;
    (void)rfkill_bt_block(1);
    umos_logf("bluetooth: off\n");
    return 0;
}

enum bt_status bt_status(void) {
    return g_status;
}

int bt_scan(struct bt_device *out, int max) {
    if (!g_master || !out || max <= 0) return -1;
    if (g_dump_len > 0)
        return parse_dump(g_dump, g_dump_len, out, max);
    if (!adapter_present()) {
        umos_logf("bluetooth: no hci0, refusing to invent peers\n");
        return -1;
    }
    /* The adapter exists but no inquiry socket is bound in this build. */
    umos_logf("bluetooth: hci0 present, inquiry transport not bound\n");
    return -1;
}

int bt_pair(const uint8_t addr[BT_ADDR_LEN]) {
    (void)addr;
    umos_logf("bluetooth: pairing needs a stack; none bound\n");
    return -1;
}

int bt_unpair(const uint8_t addr[BT_ADDR_LEN]) {
    (void)addr;
    umos_logf("bluetooth: unpair needs a stack; none bound\n");
    return -1;
}

int bt_connect(const uint8_t addr[BT_ADDR_LEN]) {
    (void)addr;
    umos_logf("bluetooth: connect needs a stack; none bound\n");
    return -1;
}

int bt_disconnect(void) {
    if (!g_assoc) return -1;
    g_assoc = 0;
    g_name[0] = '\0';
    g_status = g_master ? BT_STATUS_ON : BT_STATUS_OFF;
    umos_logf("bluetooth: disconnected\n");
    return 0;
}

int bt_is_connected(void) {
    return g_assoc;
}

int bt_link_info(struct bt_link_info *out) {
    if (!g_assoc || !out) return -1;
    *out = g_link;
    return 0;
}

const char *bt_current_name(void) {
    return g_assoc ? g_name : "";
}

int bt_profile(int index) {
    if (index < 0 || index >= BT_PROFILE_COUNT) return 0;
    return (g_profiles >> index) & 1;
}

void bt_set_profile(int index, int on) {
    if (index < 0 || index >= BT_PROFILE_COUNT) return;
    if (on) g_profiles |= (uint8_t)(1u << index);
    else    g_profiles &= (uint8_t)~(1u << index);
}

/* ---- host-gate seams -------------------------------------------------------- */

void bt_set_sysfs_root(const char *root) {
    if (!root) {
        cpy(g_root, (int)sizeof(g_root), "/sys");
        return;
    }
    cpy(g_root, (int)sizeof(g_root), root);
    int n = 0;
    while (g_root[n]) n++;
    if (n > 0 && g_root[n - 1] == '/') g_root[n - 1] = '\0';
}

void bt_set_scan_dump(const unsigned char *blob, int len) {
    if (!blob || len <= 0) {
        g_dump_len = 0;
        return;
    }
    if (len > BT_DUMP_CAP) len = BT_DUMP_CAP;
    for (int i = 0; i < len; i++) g_dump[i] = blob[i];
    g_dump_len = len;
}

void bt_forge_connected(const char *name, const struct bt_link_info *li) {
    if (!name || !li) {
        g_assoc = 0;
        g_name[0] = '\0';
        if (g_master) g_status = BT_STATUS_ON;
        return;
    }
    cpy(g_name, (int)sizeof(g_name), name);
    g_link = *li;
    g_assoc = 1;
    g_status = BT_STATUS_CONNECTED;
}
