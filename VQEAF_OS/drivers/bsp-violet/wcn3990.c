/* SPDX-License-Identifier: MIT */
/* bsp/violet/wcn3990.c - WCN3990 WiFi, reading the radio instead of reciting it.

 * There is no stored air here: every SSID on the settings screen comes from
 * an NL80211_CMD_GET_SCAN dump the kernel produced, parsed out of beacon and
 * probe IEs. On a boot where the ADSP firmware never reached /lib/firmware
 * before the lpass driver asked for it, phy0 does not exist (measured on the
 * m46 boot: /sys/class/ieee80211 empty; klog: adsp.mdt request -26, then
 * -110, then probe -16 on every retry). This seam then answers "no scan" and
 * the screen says so, which is the whole point of replacing the simulation.

 * Constants were evaluated from the kernel uapi headers, not from memory
 * (fetched 2026-11-05 into out/tmp, parsed member by member):
 * NL80211_CMD_GET_SCAN=32, TRIGGER_SCAN=33, NEW_SCAN_RESULTS=34,
 * SCAN_ABORTED=35; NL80211_ATTR_IFINDEX=3, ATTR_BSS=47; NL80211_BSS_BSSID=1,
 * BSS_FREQUENCY=2, BSS_INFORMATION_ELEMENTS=6, BSS_SIGNAL_MBM=7,
 * BSS_BEACON_IES=11; GENL_ID_CTRL=0x10, CTRL_CMD_GETFAMILY=3,
 * CTRL_ATTR_FAMILY_ID=1, CTRL_ATTR_FAMILY_NAME=2.
 */

#include "hal/include/wifi.h"
#include "wcn3990.h"
#include "core/sysio.h"
#include "core/log.h"

#include <stdint.h>
#include <stddef.h>

#define NL80211_CMD_GET_SCAN 32
#define NL80211_CMD_TRIGGER_SCAN 33
#define NL80211_CMD_NEW_SCAN_RESULTS 34
#define NL80211_CMD_SCAN_ABORTED 35
#define NL80211_ATTR_IFINDEX 3
#define NL80211_ATTR_BSS 47
#define NL80211_BSS_BSSID 1
#define NL80211_BSS_FREQUENCY 2
#define NL80211_BSS_INFORMATION_ELEMENTS 6
#define NL80211_BSS_SIGNAL_MBM 7
#define NL80211_BSS_BEACON_IES 11

#define GENL_ID_CTRL 0x10
#define CTRL_CMD_GETFAMILY 3
#define CTRL_ATTR_FAMILY_ID 1
#define CTRL_ATTR_FAMILY_NAME 2
#define NETLINK_GENERIC 16

#define NLMSG_ERROR_ 2
#define NLMSG_DONE_ 3
#define NLM_F_REQUEST_ 1
#define NLM_F_ACK_ 4
#define NLM_F_DUMP_ 0x300
#define NLA_TYPE_MASK_ 0x3fff
#define NLA_ALIGN(x) (((x) + 3u) & ~3u)

static char g_root[96] = "/sys";
static int g_master;
static int g_assoc;
static struct wifi_link_info g_link;
static char g_ssid[WIFI_MAX_SSID + 1];
static enum wifi_status g_status = WIFI_STATUS_OFF;

/* Host-gate injection: a captured GET_SCAN reply parsed by the very code the
 * phone runs. Zero length is the device's resting state: scan goes live. */
#define WCN_DUMP_CAP 65536
static unsigned char g_dump[WCN_DUMP_CAP];
static int g_dump_len;

/* ---- local string helpers (the device link carries no libc) --------------- */

static int streq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static void cpy(char *d, int cap, const char *s) {
    int n = 0;
    while (n + 1 < cap && s[n]) { d[n] = s[n]; n++; }
    d[n] = '\0';
}

static void cat_to(char *buf, int cap, const char *s) {
    int n = 0;
    while (buf[n]) n++;
    for (int i = 0; s[i] && n + 1 < cap; i++) buf[n++] = s[i];
    buf[n] = '\0';
}

/* "<root><sub>": every sysfs probe builds its path this way, so a wrong
 * root can only ever mean a wrong answer, never a read outside the fixture. */
static void syspath(char *buf, int cap, const char *sub) {
    cpy(buf, cap, g_root);
    cat_to(buf, cap, sub);
}

static void trim(char *s) {
    int n = 0;
    while (s[n]) n++;
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' '))
        s[--n] = '\0';
}

/* ---- netlink wire: little-endian byte access, so host and phone agree ----- */

static uint32_t le32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint16_t le16(const unsigned char *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

static void put32(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24);
}

static void put16(unsigned char *p, uint16_t v) {
    p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
}

/* nlmsghdr(16) + genlmsghdr(4); returns the offset where attrs begin */
static int nl_start(unsigned char *m, uint16_t type, uint16_t flags, uint8_t cmd) {
    static uint32_t counter;
    put32(m, 20);
    put16(m + 4, type);
    put16(m + 6, flags);
    put32(m + 8, ++counter);
    put32(m + 12, 0); /* pid 0: bind assigns our portid */
    m[16] = cmd;
    m[17] = 1; /* genl ops version */
    m[18] = 0;
    m[19] = 0;
    return 20;
}

static void nl_attr(unsigned char *m, int *off, uint16_t type,
                    const unsigned char *data, int dlen) {
    int pad = (int)NLA_ALIGN((uint32_t)(dlen + 4));
    put16(m + *off, (uint16_t)(dlen + 4));
    put16(m + *off + 2, type);
    for (int i = 0; i < dlen; i++) m[*off + 4 + i] = data[i];
    for (int i = 4 + dlen; i < pad; i++) m[*off + i] = 0;
    *off += pad;
    put32(m, (uint32_t)*off);
}

static void nl_attr_u32(unsigned char *m, int *off, uint16_t type, uint32_t v) {
    unsigned char b[4];
    put32(b, v);
    nl_attr(m, off, type, b, 4);
}

/* ---- the parser: what the gates actually measure -------------------------- */

/* IEs carry the name and the security: element 0 is the SSID, RSN (id 48)
 * holding an 00-0F-AC-06 AKM is WPA3-SAE, any other RSN is WPA2, the
 * 00:50:F2 vendor element (id 221) with WPA sub-type 2 is WPA, and nothing
 * at all is Open. The AKM bytes are found by sliding a 4-byte window over
 * the RSN body: exact offset arithmetic on a stream firmwares pad
 * differently is not worth the fragility. */
static int parse_bss_attrs(const unsigned char *p, int len,
                           struct wifi_scan_result *r) {
    const unsigned char *ies = 0;
    int ies_len = 0;
    int have_ssid = 0;

    for (int i = 0; i + 4 <= len; ) {
        unsigned alen = le16(p + i);
        unsigned atype = le16(p + i + 2) & NLA_TYPE_MASK_;
        if (alen < 4 || i + (int)alen > len) break;
        const unsigned char *d = p + i + 4;
        int dlen = (int)alen - 4;

        if (atype == NL80211_BSS_BSSID && dlen >= WIFI_MAX_BSSID) {
            for (int b = 0; b < WIFI_MAX_BSSID; b++) r->bssid[b] = d[b];
        } else if (atype == NL80211_BSS_FREQUENCY && dlen >= 4) {
            uint32_t f = le32(d);
            r->channel = (uint8_t)(f >= 5950 ? (f - 5950) / 5 + 193 :
                                   f > 2500 ? (f - 5000) / 5 :
                                               (f - 2407) / 5);
        } else if (atype == NL80211_BSS_SIGNAL_MBM && dlen >= 4) {
            /* the kernel fills this as dBm*100 despite the "1000*dBm" comment
             * in the uapi header; wpa_supplicant divides by 100 for the same
             * reason */
            int32_t s = (int32_t)le32(d);
            r->rssi = (int8_t)(s / 100);
        } else if ((atype == NL80211_BSS_INFORMATION_ELEMENTS ||
                    atype == NL80211_BSS_BEACON_IES) && dlen > 0 && !ies) {
            ies = d;
            ies_len = dlen;
        }
        i += (int)NLA_ALIGN(alen);
    }

    if (!ies) return -1;

    r->auth = WIFI_AUTH_OPEN;
    int saw_rsn = 0;
    for (int i = 0; i + 2 <= ies_len; ) {
        int id = ies[i], elen = ies[i + 1];
        if (i + 2 + elen > ies_len) break;
        const unsigned char *e = ies + i + 2;
        if (id == 0) {
            int n = elen < WIFI_MAX_SSID ? elen : WIFI_MAX_SSID;
            for (int b = 0; b < n; b++) r->ssid[b] = (char)e[b];
            r->ssid[n] = '\0';
            have_ssid = 1;
        } else if (id == 48) {
            saw_rsn = 1;
            for (int k = 0; k + 4 <= elen; k++) {
                if (e[k] == 0x00 && e[k + 1] == 0x0F && e[k + 2] == 0xAC &&
                    e[k + 3] == 0x06) {
                    r->auth = WIFI_AUTH_WPA3_SAE;
                    break;
                }
            }
        } else if (id == 221 && elen >= 6 && e[0] == 0x00 && e[1] == 0x50 &&
                   e[2] == 0xF2 && e[5] == 0x02) {
            if (r->auth != WIFI_AUTH_WPA3_SAE) r->auth = WIFI_AUTH_WPA_PSK;
        }
        i += 2 + elen;
    }
    if (saw_rsn && r->auth != WIFI_AUTH_WPA3_SAE) r->auth = WIFI_AUTH_WPA2_PSK;
    return have_ssid ? 0 : -1;
}

static void zero_row(struct wifi_scan_result *r) {
    unsigned char *b = (unsigned char *)r;
    for (size_t i = 0; i < sizeof(*r); i++) b[i] = 0;
    r->rssi = -120;
}

/* Walk datagrams (nlmsghdr + genlmsghdr + attrs) and fill rows. No length
 * field is ever trusted to stay inside the buffer. */
static int parse_dump(const unsigned char *p, int len,
                      struct wifi_scan_result *out, int max) {
    int n = 0;
    int off = 0;
    while (off + 16 <= len) {
        uint32_t mlen = le32(p + off);
        uint16_t mtype = le16(p + off + 4); /* nlmsg_type, not inside the length */
        if (mlen < 16 || off + (int)mlen > len) break;
        if (mtype == NL80211_CMD_GET_SCAN && n < max) {
            const unsigned char *a = p + off + 20; /* past nlmsghdr+genlmsghdr */
            int alen = (int)mlen - 20;
            for (int i = 0; alen >= 4 && i + 4 <= alen; ) {
                unsigned al = le16(a + i);
                unsigned at = le16(a + i + 2) & NLA_TYPE_MASK_;
                if (al < 4 || i + (int)al > alen) break;
                if (at == NL80211_ATTR_BSS && n < max) {
                    struct wifi_scan_result *r = &out[n];
                    zero_row(r);
                    if (parse_bss_attrs(a + i + 4, (int)al - 4, r) == 0) n++;
                }
                i += (int)NLA_ALIGN(al);
            }
        }
        off += (int)NLA_ALIGN(mlen);
    }

    /* strongest signal first: that is the order a thumb reads the list */
    for (int i = 1; i < n; i++) {
        struct wifi_scan_result tmp = out[i];
        int k = i - 1;
        while (k >= 0 && out[k].rssi < tmp.rssi) {
            out[k + 1] = out[k];
            k--;
        }
        out[k + 1] = tmp;
    }
    return n;
}

/* ---- live nl80211 transport (answers on the phone, -38 on the host) ------- */

static int resolve_family(int fd, uint32_t *fam) {
    unsigned char m[128];
    unsigned char rsp[4096];
    static const char name[] = "nl80211"; /* 7 bytes: the kernel wants no NUL */

    int off = nl_start(m, GENL_ID_CTRL, NLM_F_REQUEST_, CTRL_CMD_GETFAMILY);
    nl_attr(m, &off, CTRL_ATTR_FAMILY_NAME, (const unsigned char *)name, 7);
    if (sysio_nl_send(fd, m, off) < 0) return -1;
    int n = sysio_nl_recv(fd, rsp, (int)sizeof(rsp), 1500);
    if (n < 24) return -1;
    const unsigned char *a = rsp + 20;
    int alen = n - 20;
    for (int i = 0; i + 4 <= alen; ) {
        unsigned al = le16(a + i);
        unsigned at = le16(a + i + 2) & NLA_TYPE_MASK_;
        if (al < 4 || i + (int)al > alen) break;
        if (at == CTRL_ATTR_FAMILY_ID && al >= 8) {
            *fam = le32(a + i + 4);
            return 0;
        }
        i += (int)NLA_ALIGN(al);
    }
    return -1;
}

/* trigger, wait for the one event that says the driver is done, dump.
 * Returns bytes collected into g_dump, or negative. */
static int run_live_scan(uint32_t fam, uint32_t ifindex, int fd) {
    unsigned char m[64];
    unsigned char rsp[2048];

    int off = nl_start(m, (uint16_t)fam, NLM_F_REQUEST_ | NLM_F_ACK_,
                       NL80211_CMD_TRIGGER_SCAN);
    nl_attr_u32(m, &off, NL80211_ATTR_IFINDEX, ifindex);
    if (sysio_nl_send(fd, m, off) < 0) return -1;
    int an = sysio_nl_recv(fd, rsp, (int)sizeof(rsp), 1000);
    /* an ACK is an NLMSG_ERROR whose nlmsgerr.error sits right after the 16-byte header */
    if (an >= 20 && le16(rsp + 4) == NLMSG_ERROR_ && le32(rsp + 16) != 0) {
        umos_logf("wifi: trigger refused\n");
        return -1;
    }

    int waited = 0;
    int finished = 0;
    while (waited < 8000) {
        int n = sysio_nl_recv(fd, rsp, (int)sizeof(rsp), 500);
        waited += 500;
        if (n < 16) continue;
        uint16_t type = le16(rsp + 4);
        if (type == NL80211_CMD_NEW_SCAN_RESULTS) { finished = 1; break; }
        if (type == NL80211_CMD_SCAN_ABORTED) return -1;
    }
    if (!finished)
        umos_logf("wifi: no NEW_SCAN_RESULTS inside 8s; dumping driver cache\n");

    off = nl_start(m, (uint16_t)fam, NLM_F_REQUEST_ | NLM_F_DUMP_,
                   NL80211_CMD_GET_SCAN);
    nl_attr_u32(m, &off, NL80211_ATTR_IFINDEX, ifindex);
    if (sysio_nl_send(fd, m, off) < 0) return -1;

    int got = 0;
    while (got < WCN_DUMP_CAP) {
        int n = sysio_nl_recv(fd, g_dump + got, WCN_DUMP_CAP - got, 1500);
        if (n <= 0) break;
        got += n;
        int done = 0;
        for (int o = 0; o + 16 <= got; ) {
            uint32_t ml = le32(g_dump + o);
            if (ml < 16 || o + (int)ml > got) break;
            if (le16(g_dump + o + 4) == NLMSG_DONE_) done = 1;
            o += (int)NLA_ALIGN(ml);
        }
        if (done) break;
    }
    return got > 0 ? got : -110;
}

static int scan_now(struct wifi_scan_result *results, int max) {
    char phy[24];
    char p[192];
    int ifindex;
    uint32_t fam;

    syspath(p, (int)sizeof(p), "/class/ieee80211");
    if (sysio_first_entry(p, "phy", phy, (int)sizeof(phy)) < 0) {
        umos_logf("wifi: no phy under ieee80211; radio not up on this boot\n");
        return -1;
    }
    syspath(p, (int)sizeof(p), "/class/net/wlan0/ifindex");
    if (sysio_read_int(p, &ifindex) < 0 || ifindex <= 0) {
        umos_logf("wifi: phy %s but no wlan0 ifindex\n", phy);
        return -1;
    }

    int fd = sysio_nl_open(NETLINK_GENERIC);
    if (fd < 0) {
        umos_logf("wifi: netlink unavailable here (rc %d)\n", fd);
        return -1;
    }
    int n = -1;
    if (resolve_family(fd, &fam) == 0) {
        int len = run_live_scan(fam, (uint32_t)ifindex, fd);
        if (len > 0) {
            n = parse_dump(g_dump, len, results, max);
            umos_logf("wifi: live scan answered %d rows\n", n);
        } else {
            umos_logf("wifi: live scan rc %d\n", len);
        }
    } else {
        umos_logf("wifi: nl80211 family not resolvable\n");
    }
    sysio_nl_close(fd);
    return n;
}

/* ---- rfkill: the one radio truth a userspace toggle can reach ------------- */

/* Find the rfkill node whose type says wlan and write its soft switch.
 * Returns 0 when a node answered, -1 when the board shows none - the usual
 * state while ADSP never booted. */
static int rfkill_wlan_block(int block) {
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
        if (!streq(t, "wlan")) continue;

        int m = 0;
        while (p[m]) m++;
        m -= 5; /* strlen("/type") */
        if (m < 0) m = 0;
        p[m] = '\0';
        cat_to(p, (int)sizeof(p), "/soft");
        if (sysio_write_str(p, block ? "1\n" : "0\n") < 0) return -1;
        return 0;
    }
    return -1;
}

/* ---- HAL seam -------------------------------------------------------------- */

int wifi_init(void) {
    g_master = 0;
    g_assoc = 0;
    g_dump_len = 0;
    g_ssid[0] = '\0';
    g_status = WIFI_STATUS_OFF;
    umos_logf("wifi: WCN3990 seam bound to the real radio\n");
    return 0;
}

/* The switch is the user's intent; what the radio can do about it is
 * reported by the next scan, not claimed here. */
int wifi_on(void) {
    g_master = 1;
    g_status = WIFI_STATUS_ON;
    int rk = rfkill_wlan_block(0);
    umos_logf("wifi: on, wlan rfkill %s\n", rk == 0 ? "unblocked" : "absent");
    return 0;
}

int wifi_off(void) {
    g_master = 0;
    g_assoc = 0;
    g_ssid[0] = '\0';
    g_status = WIFI_STATUS_OFF;
    (void)rfkill_wlan_block(1);
    umos_logf("wifi: off\n");
    return 0;
}

enum wifi_status wifi_status(void) {
    return g_status;
}

int wifi_scan(struct wifi_scan_result *results, int max_results) {
    if (!g_master || !results || max_results <= 0) return -1;
    if (g_dump_len > 0)
        return parse_dump(g_dump, g_dump_len, results, max_results);
    return scan_now(results, max_results);
}

/* Associating is a supplicant's job: four-way handshake, keys, DHCP.
 * qeafinit binds none of that yet, so the honest answer is "no". */
int wifi_connect(const struct wifi_config *cfg) {
    (void)cfg;
    umos_logf("wifi: association needs a supplicant; none bound\n");
    return -1;
}

int wifi_disconnect(void) {
    if (!g_assoc) return -1;
    g_assoc = 0;
    g_ssid[0] = '\0';
    g_status = g_master ? WIFI_STATUS_ON : WIFI_STATUS_OFF;
    umos_logf("wifi: disconnected\n");
    return 0;
}

int wifi_is_connected(void) {
    return g_assoc;
}

int wifi_link_info(struct wifi_link_info *out) {
    if (!g_assoc || !out) return -1;
    *out = g_link;
    return 0;
}

const char *wifi_current_ssid(void) {
    return g_assoc ? g_ssid : "";
}

/* ---- host-gate seams -------------------------------------------------------- */

void wcn_set_sysfs_root(const char *root) {
    if (!root) {
        cpy(g_root, (int)sizeof(g_root), "/sys");
        return;
    }
    cpy(g_root, (int)sizeof(g_root), root);
    int n = 0;
    while (g_root[n]) n++;
    if (n > 0 && g_root[n - 1] == '/') g_root[n - 1] = '\0';
}

void wcn_set_scan_dump(const unsigned char *blob, int len) {
    if (!blob || len <= 0) {
        g_dump_len = 0;
        return;
    }
    if (len > WCN_DUMP_CAP) len = WCN_DUMP_CAP;
    for (int i = 0; i < len; i++) g_dump[i] = blob[i];
    g_dump_len = len;
}

void wcn_forge_connected(const char *ssid, const struct wifi_link_info *li) {
    if (!ssid || !li) {
        g_assoc = 0;
        g_ssid[0] = '\0';
        if (g_master) g_status = WIFI_STATUS_ON;
        return;
    }
    cpy(g_ssid, (int)sizeof(g_ssid), ssid);
    g_link = *li;
    g_assoc = 1;
    g_status = WIFI_STATUS_CONNECTED;
}
