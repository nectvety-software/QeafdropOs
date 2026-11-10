/* SPDX-License-Identifier: MIT */
/* fs/rescue.c - the QEAFRESCUE command channel, in the image itself.
 *
 * The volume is built in RAM by fat16.c and served to the PC as a LUN. A host
 * drops QEAF.CMD onto it; this loop notices the new bytes, runs the verbs and
 * writes RESPONSE.TXT back. Everything is a real FAT16 file - no separate
 * socket, no hidden side channel.
 *
 * The flash verb is the one privileged action. It refuses anything that is
 * not exactly "flash recovery BOOTIMG.BIN confirm", checks ANDROID! magic and
 * the p77 size, writes the staged image to /dev/mmcblk0p77, then READS THE
 * PARTITION BACK and compares every byte before it says "verified".
 *
 * Host gate: rescue_set_partition_path() points the same code at a fixture
 * file, so the loop and the verification are measured without a phone.
 */

#include "fs/rescue.h"
#include "fs/fat16.h"
#include "core/sysio.h"
#include "core/log.h"
#include "qeaf_version.h"
#include "hal/include/usb_gadget.h"
#include "hal/include/display.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifdef __linux__
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/reboot.h>
#include <sys/syscall.h>
#include <linux/reboot.h>
#endif

#define CMD_CAP   1024
#define RESP_CAP  8192
#define LOG_CAP   8192
#define P77_SIZE  (64u * 1024u * 1024u)

static const char *g_partition = "/dev/mmcblk0p77";
static rescue_snapshot_fn g_snapshot;
static rescue_language_fn g_language;
static uint8_t g_last[CMD_CAP];
static int g_last_len = -1;
static int g_result;
static char g_log[LOG_CAP];
static int g_log_len;

static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }
static int starts(const char *s, const char *p) {
    while (*p) { if (*s != *p) return 0; s++; p++; }
    return 1;
}
static int eq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}
static void cat(char *d, int cap, const char *s) {
    int n = slen(d);
    while (*s && n + 1 < cap) d[n++] = *s++;
    d[n] = '\0';
}
static void cat_u(char *d, int cap, unsigned v) {
    char t[12]; int k = 0;
    do { t[k++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (k > 0) { int n = slen(d); if (n + 1 >= cap) return;
                    d[n] = t[--k]; d[n + 1] = '\0'; }
    if (!k && !slen(d)) cat(d, cap, "0");
}
static void put(struct fat16_volume *vol, const char *name, const char *text) {
    fat16_write_file(vol, name, (const uint8_t *)text, (size_t)slen(text));
}
static void note(const char *s) {
    int n = slen(s), i;
    if (g_log_len + n + 2 >= LOG_CAP) {
        /* keep the tail: the last verification line is what matters */
        int keep = LOG_CAP / 2;
        memmove(g_log, g_log + g_log_len - keep, (size_t)keep);
        g_log_len = keep;
    }
    for (i = 0; i < n && g_log_len + 1 < LOG_CAP; i++)
        g_log[g_log_len++] = s[i];
    if (g_log_len + 1 < LOG_CAP) g_log[g_log_len++] = '\n';
    g_log[g_log_len] = '\0';
}

static const char HELP[] =
    "QEAFRESCUE command channel (VQEAF-SYNC/1)\n"
    "=========================================\n"
    "Write one or more verbs per line into QEAF.CMD, flush the volume (do\n"
    "not eject), then read RESPONSE.TXT by RAW SECTOR - the mounted Windows\n"
    "view is stale. Every batch should end with 'echo <nonce>' so the host\n"
    "can tell a fresh answer from the previous one.\n\n"
    "  help                 show this text\n"
    "  status               OS, USB mode, cable, LUN sectors\n"
    "  wifi                 /sys/class/net + kmsg wlan lines (driver probe)\n"
    "  sync                 republish DEVINFO.TXT\n"
    "  usb charge|mtp|ptp   choose the USB data mode\n"
    "  rotate 0|90|180|270|auto   screen orientation / auto-rotate\n"
    "  lang vi|en|zh        switch the UI language and persist it\n"
    "  echo TEXT            echoed back verbatim (nonce)\n"
    "  ls                   root entries of this volume\n"
    "  kmsg                 tail of the homo: kernel-log lines (boot trace)\n"
    "  snap                 dump the live scanout to SCREEN.RAW (RGB565)\n"
    "  reboot system|recovery|bootloader\n"
    "                       restart the phone; recovery boots p77\n"
    "  flash recovery BOOTIMG.BIN confirm\n"
    "                       write the staged image to p77, read it back\n"
    "                       and compare byte for byte before answering\n\n"
    "Only 'recovery' (p77) is writable. p76/boot, userdata and misc are\n"
    "never touched. The word 'confirm' is mandatory. HELP.TXT, SUMMARY.TXT,\n"
    "RESPONSE.TXT, QEAFBOOT.LOG and DEVINFO.TXT are the channel's own files.\n";

static const char SUMMARY[] =
    QEAF_OS_STRING " - rescue volume\n\n"
    "Built in RAM by fat16.c; no partition has to survive for these bytes\n"
    "to exist. Read with raw sectors, not the Windows mounted view.\n\n"
    "--- commands (QEAF.CMD, one per line)\n"
    "  help  status  wifi  sync  usb MODE  echo TEXT  ls\n"
    "  rotate 0|90|180|270|auto\n"
    "  lang vi|en|zh       switch the UI language\n"
    "  snap                 write SCREEN.RAW (240x520 RGB565) for the host\n"
    "  flash recovery BOOTIMG.BIN confirm\n\n"
    "--- sync\n"
    "  DEVINFO.TXT carries VQEAF-SYNC/1 machine state for VQEAF Tool.\n";

void rescue_set_partition_path(const char *path) {
    g_partition = path ? path : "/dev/mmcblk0p77";
}

void rescue_set_snapshot_hook(rescue_snapshot_fn fn) {
    g_snapshot = fn;
}

void rescue_set_language_hook(rescue_language_fn fn) {
    g_language = fn;
}

int rescue_last_result(void) { return g_result; }

void rescue_init(struct fat16_volume *vol) {
    g_last_len = -1;
    g_result = 0;
    g_log_len = 0;
    g_log[0] = '\0';
    if (!vol) return;
    put(vol, "HELP.TXT", HELP);
    put(vol, "SUMMARY.TXT", SUMMARY);
    note("[    0.000] N qeaf: " QEAF_OS_STRING " starting (pid 1)");
    note("[    0.010] N qeaf: rescue channel bound to the RAM LUN");
    put(vol, "QEAFBOOT.LOG", g_log);
    put(vol, "RESPONSE.TXT", "no command has run yet\n");
    umos_logf("rescue: channel files published (QEAF.CMD reader ready)\n");
}

/* ---- one flash ------------------------------------------------------------ */

static void do_flash(struct fat16_volume *vol, char *resp, int cap) {
    const uint8_t *image = NULL;
    size_t size = 0;
    int rc;

    if (fat16_file_span(vol, "BOOTIMG.BIN", &image, &size) < 0) {
        cat(resp, cap, "flash: REFUSED - BOOTIMG.BIN missing or fragmented\n");
        note("[    -    ] E qeaf: flash refused, no contiguous BOOTIMG.BIN");
        g_result = -1;
        return;
    }
    if (size > P77_SIZE || size < 4096 || memcmp(image, "ANDROID!", 8) != 0) {
        cat(resp, cap, "flash: REFUSED - ANDROID! magic or p77 size check failed\n");
        note("[    -    ] E qeaf: flash refused, bad header or size");
        g_result = -2;
        return;
    }
    note("[    -    ] N qeaf: lun: running flash recovery BOOTIMG.BIN confirm");
    rc = sysio_block_write(g_partition, image, size);
    if (rc != 0) {
        char line[160];
        line[0] = '\0';
        cat(line, sizeof(line), "flash: FAILED - partition write returned an error (errno ");
        cat_u(line, sizeof(line), (unsigned)(sysio_last_errno() < 0
                                             ? -sysio_last_errno()
                                             : sysio_last_errno()));
        cat(line, sizeof(line), ")");
        cat(resp, cap, line);
        cat(resp, cap, "\n");
        note("[    -    ] E qeaf: flash: partition write failed");
        note(line);
        g_result = -3;
        return;
    }
    rc = sysio_block_verify(g_partition, image, size);
    if (rc != 0) {
        cat(resp, cap, "flash: FAILED - readback did not match\n");
        note("[    -    ] E qeaf: flash: readback mismatch");
        g_result = -4;
        return;
    }
    cat(resp, cap, "flash: written and verified byte for byte\n");
    {
        char line[160];
        line[0] = '\0';
        cat(line, sizeof(line),
            "[    -    ] N qeaf: lun: verified - recovery and BOOTIMG.BIN "
            "agree byte for byte over ");
        cat_u(line, sizeof(line), (unsigned)size);
        cat(line, sizeof(line), " bytes");
        note(line);
    }
    g_result = 1;
}

/* ---- verb handling -------------------------------------------------------- */

static void run_verbs(struct fat16_volume *vol, const char *cmd, int len,
                      char *resp, int cap) {
    int i = 0;
    while (i < len) {
        char line[256];
        int n = 0;
        while (i < len && (cmd[i] == '\n' || cmd[i] == '\r')) i++;
        while (i < len && cmd[i] != '\n' && cmd[i] != '\r' && n + 1 < (int)sizeof(line))
            line[n++] = cmd[i++];
        line[n] = '\0';
        while (n > 0 && (line[n - 1] == ' ' || line[n - 1] == '\t')) line[--n] = '\0';
        if (!n) continue;

        cat(resp, cap, "### ");
        cat(resp, cap, line);
        cat(resp, cap, "\n");
        if (starts(line, "echo ")) {
            cat(resp, cap, line + 5);
            cat(resp, cap, "\n");
        } else if (eq(line, "help")) {
            cat(resp, cap, HELP);
        } else if (starts(line, "rotate ")) {
            int o = display_parse_orientation(line + 7);
            if (o == -2) {
                display_set_auto_rotate(1);
                cat(resp, cap, "rotate: auto-rotate on\n");
            } else if (o >= 0) {
                display_set_auto_rotate(0);
                cat(resp, cap, "rotate: orientation -> ");
                cat(resp, cap, display_orientation_name(
                        display_set_orientation((enum display_orientation)o)));
                cat(resp, cap, "\n");
            } else {
                cat(resp, cap, "rotate: unknown value (0|90|180|270|auto)\n");
            }
        } else if (starts(line, "lang ")) {
            const char *v = line + 5;
            int lang = -1;
            if (eq(v, "vi")) lang = 0;
            else if (eq(v, "en")) lang = 1;
            else if (eq(v, "zh")) lang = 3;
            if (lang < 0) {
                cat(resp, cap, "lang: unknown value (vi|en|zh)\n");
            } else if (g_language == NULL) {
                cat(resp, cap, "lang: unavailable (no language hook)\n");
            } else if (g_language(lang) == 0) {
                cat(resp, cap, "lang: applied ");
                cat(resp, cap, v);
                cat(resp, cap, "\n");
                note("[    -    ] N qeaf: lang applied from QEAF.CMD");
                g_result = 1;
            } else {
                cat(resp, cap, "lang: refused (apply failed)\n");
                note("[    -    ] E qeaf: lang apply failed");
                g_result = -5;
            }
        } else if (eq(line, "status")) {
            cat(resp, cap, QEAF_OS_STRING "; device violet; usb_mode=");
            cat(resp, cap, usb_gadget_mode_name(usb_gadget_mode()));
            cat(resp, cap, "; usb_cable=");
            cat_u(resp, cap, (unsigned)usb_gadget_cable());
            cat(resp, cap, "; orientation=");
            cat(resp, cap, display_orientation_name(display_orientation()));
            cat(resp, cap, "; auto_rotate=");
            cat_u(resp, cap, (unsigned)display_auto_rotate());
            cat(resp, cap, "; control_lun=QEAFRESCUE\n");
        } else if (starts(line, "wifi ") || eq(line, "wifi")) {
            int sub = 0; /* 0 = tổng, 1 = net, 2 = dmesg, 3 = sdio, 4 = mod, 5 = fw */
            if (starts(line, "wifi ")) {
                const char *subname = line + 5;
                if (eq(subname, "net")) sub = 1;
                else if (eq(subname, "dmesg")) sub = 2;
                else if (eq(subname, "sdio")) sub = 3;
                else if (eq(subname, "mod")) sub = 4;
                else if (eq(subname, "fw")) sub = 5;
                else if (eq(subname, "firmware")) sub = 5;
                else { cat(resp, cap, "wifi: unknown sub (net|dmesg|sdio|mod|fw)\n"); }
            }
#ifdef __linux__
            if (sub == 5) {
                /* Firmware probe: liệt kê các thư mục firmware yêu cầu. */
                const char *dirs[] = {"/vendor/firmware", "/system/vendor/firmware",
                                      "/etc/firmware", "/system/etc/firmware",
                                      "/lib/firmware", "/usr/share/firmware"};
                for (unsigned di = 0; di < sizeof(dirs)/sizeof(dirs[0]); di++) {
                    DIR *d = opendir(dirs[di]);
                    cat(resp, cap, "wifi fw: ");
                    cat(resp, cap, dirs[di]);
                    cat(resp, cap, ": ");
                    if (d == NULL) cat(resp, cap, "absent\n");
                    else {
                        struct dirent *e; int any = 0, hit = 0;
                        while ((e = readdir(d)) != NULL) {
                            if (e->d_name[0] == '.') continue;
                            if (strstr(e->d_name, "WCN") != NULL || strstr(e->d_name, "wcn") != NULL ||
                                strstr(e->d_name, "wlan") != NULL || strstr(e->d_name, "qca") != NULL ||
                                strstr(e->d_name, "firmware") != NULL) hit++;
                            if (any < 40) { if (any++) cat(resp, cap, ", "); cat(resp, cap, e->d_name); }
                        }
                        closedir(d);
                        if (any == 0) cat(resp, cap, "(empty)");
                        else { cat(resp, cap, " ("); cat_u(resp, cap, (unsigned)hit); cat(resp, cap, " wlan-related)"); }
                        cat(resp, cap, "\n");
                    }
                }
                note("[    -    ] N qeaf: wifi fw probe answered");
                /* The packaging step writes this report into the initramfs.
                 * Print it verbatim so the host sees which WCN3990 blobs the
                 * image actually carries, not just which directories exist. */
                {
                    int fd = open("/etc/qeaf/radio-firmware-status.txt",
                                  O_RDONLY | O_CLOEXEC);
                    cat(resp, cap, "wifi fw report: ");
                    if (fd < 0) {
                        cat(resp, cap, "absent\n");
                    } else {
                        char rep[1024];
                        ssize_t r = read(fd, rep, sizeof(rep) - 1);
                        close(fd);
                        if (r <= 0) {
                            cat(resp, cap, "empty\n");
                        } else {
                            rep[r] = '\0';
                            cat(resp, cap, "\n");
                            cat(resp, cap, rep);
                            if (rep[r - 1] != '\n') cat(resp, cap, "\n");
                        }
                    }
                }
            } else if (sub == 1) {
                DIR *d = opendir("/sys/class/net");
                cat(resp, cap, "wifi net: ");
                if (d == NULL) cat(resp, cap, "cannot open\n");
                else {
                    struct dirent *e;
                    char pp[96], buf[128];
                    int cnt = 0;
                    while ((e = readdir(d)) != NULL) {
                        if (e->d_name[0] == '.') continue;
                        if (cnt++) cat(resp, cap, " | ");
                        cat(resp, cap, e->d_name);
                        snprintf(pp, sizeof(pp), "/sys/class/net/%s/operstate", e->d_name);
                        {
                            int f = open(pp, O_RDONLY | O_CLOEXEC);
                            if (f >= 0) { ssize_t n = read(f, buf, sizeof(buf)-1); close(f);
                                if (n > 0) { buf[n] = 0; while (n > 0 && (buf[n-1]=='\n'||buf[n-1]=='\r')) buf[--n]=0; cat(resp, cap, " operstate="); cat(resp, cap, buf); } }
                        }
                        snprintf(pp, sizeof(pp), "/sys/class/net/%s/address", e->d_name);
                        {
                            int f = open(pp, O_RDONLY | O_CLOEXEC);
                            if (f >= 0) { ssize_t n = read(f, buf, sizeof(buf)-1); close(f);
                                if (n > 0) { buf[n] = 0; while (n > 0 && (buf[n-1]=='\n'||buf[n-1]=='\r')) buf[--n]=0; cat(resp, cap, " addr="); cat(resp, cap, buf); } }
                        }
                        if (cnt > 16) break;
                    }
                    closedir(d);
                    cat(resp, cap, "\n");
                }
                note("[    -    ] N qeaf: wifi net probe answered");
            } else if (sub == 2) {
                int fd = open("/dev/kmsg", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
                cat(resp, cap, "wifi dmesg: ");
                if (fd < 0) cat(resp, cap, "kmsg unavailable\n");
                else {
                    char buf[4096]; char tail[7168]; int tl = 0, found = 0; tail[0] = 0;
                    for (;;) {
                        ssize_t r = read(fd, buf, sizeof(buf)-1);
                        if (r <= 0) break;
                        buf[r] = 0;
                        char *msg = strchr(buf, ';'); msg = msg ? msg + 1 : buf;
                        const char *keys[] = {"wlan", "WCN", "cfg80211", "mac80211", "wifi", " firmware", "firmware", "BSS", "associat", "SDIO"};
                        int hit = 0;
                        for (unsigned k = 0; k < sizeof(keys)/sizeof(keys[0]); k++) if (strstr(msg, keys[k]) != NULL) hit = 1;
                        if (!hit) continue;
                        found++;
                        int len = slen(msg);
                        while (len > 0 && (msg[len-1]=='\n'||msg[len-1]=='\r')) len--;
                        if (tl + len + 2 >= (int)sizeof(tail)) { int keep = (int)sizeof(tail)/2; memmove(tail, tail+tl-keep, (size_t)keep); tl = keep; }
                        if (tl > 0) tail[tl++] = ' ';
                        int copy = len;
                        if (tl + copy >= (int)sizeof(tail)) copy = (int)sizeof(tail)-tl-1;
                        if (copy > 0) { memcpy(tail+tl, msg, (size_t)copy); tl += copy; tail[tl] = 0; }
                    }
                    close(fd);
                    if (!found || tl == 0) cat(resp, cap, "no wlan lines\n");
                    else { cat_u(resp, cap, (unsigned)found); cat(resp, cap, " lines: "); cat(resp, cap, tail); cat(resp, cap, "\n"); }
                }
                note("[    -    ] N qeaf: wifi dmesg probe answered");
            } else if (sub == 3) {
                const char *dirs[] = {"/sys/bus/mmc/devices", "/sys/bus/mmc/drivers", "/sys/bus/mmc_link"};
                for (unsigned di = 0; di < 3; di++) {
                    DIR *d = opendir(dirs[di]);
                    cat(resp, cap, "wifi sdio: ");
                    cat(resp, cap, dirs[di]);
                    cat(resp, cap, ": ");
                    if (d == NULL) cat(resp, cap, "absent\n");
                    else {
                        struct dirent *e; int any = 0;
                        while ((e = readdir(d)) != NULL) { if (e->d_name[0]=='.') continue; if (any++) cat(resp, cap, ", "); cat(resp, cap, e->d_name); if (any > 20) break; }
                        closedir(d);
                        if (!any) cat(resp, cap, "(empty)");
                        cat(resp, cap, "\n");
                    }
                }
                note("[    -    ] N qeaf: wifi sdio probe answered");
            } else if (sub == 4) {
                DIR *d = opendir("/sys/module");
                cat(resp, cap, "wifi mod: /sys/module: ");
                if (d == NULL) cat(resp, cap, "absent\n");
                else {
                    struct dirent *e; int any = 0;
                    while ((e = readdir(d)) != NULL) {
                        if (e->d_name[0]=='.') continue;
                        if (strstr(e->d_name, "wlan") == NULL && strstr(e->d_name, "WCN") == NULL &&
                            strstr(e->d_name, "cfg80211") == NULL && strstr(e->d_name, "mac80211") == NULL &&
                            strstr(e->d_name, "bluetooth") == NULL) continue;
                        if (any++) cat(resp, cap, ", ");
                        cat(resp, cap, e->d_name);
                    }
                    closedir(d);
                    if (!any) cat(resp, cap, "(no wlan/cfg/mac80211 module)");
                    cat(resp, cap, "\n");
                }
                DIR *lm = opendir("/lib/modules");
                cat(resp, cap, "wifi mod: /lib/modules: ");
                if (lm == NULL) cat(resp, cap, "absent\n");
                else { struct dirent *e; int any = 0; while ((e = readdir(lm)) != NULL) { if (e->d_name[0]=='.') continue; if (any++) cat(resp, cap, ", "); cat(resp, cap, e->d_name); } closedir(lm); if (!any) cat(resp, cap, "(empty)"); cat(resp, cap, "\n"); }
                note("[    -    ] N qeaf: wifi mod probe answered");
            } else {
            /* Driver probe, không đụng firmware: liệt kê netdev và lọc dmesg
             * những dòng wlan/WCN/cfg80211. Không có dòng nào = chưa có driver. */
            DIR *d = opendir("/sys/class/net");
            cat(resp, cap, "wifi: /sys/class/net: ");
            if (d == NULL) {
                cat(resp, cap, "cannot open (no netdev?)\n");
            } else {
                struct dirent *e;
                int any = 0;
                while ((e = readdir(d)) != NULL) {
                    if (e->d_name[0] == '.') continue;
                    if (any) cat(resp, cap, ", ");
                    cat(resp, cap, e->d_name);
                    any = 1;
                }
                closedir(d);
                if (!any) cat(resp, cap, "(empty)");
                cat(resp, cap, "\n");
                char addr_path[80];
                cat(resp, cap, "wifi: wlan0 present: ");
                snprintf(addr_path, sizeof(addr_path), "/sys/class/net/wlan0");
                {
                    DIR *w = opendir(addr_path);
                    cat(resp, cap, w ? "yes\n" : "no\n");
                    if (w) closedir(w);
                }
            }
            {
                int fd = open("/dev/kmsg", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
                if (fd < 0) {
                    cat(resp, cap, "wifi: kmsg unavailable\n");
                } else {
                    char buf[4096];
                    char tail[4096];
                    int tl = 0, found = 0;
                    tail[0] = '\0';
                    for (;;) {
                        ssize_t r = read(fd, buf, sizeof(buf) - 1);
                        if (r <= 0) break;
                        buf[r] = '\0';
                        char *msg = strchr(buf, ';');
                        msg = msg ? msg + 1 : buf;
                        char *low = msg;
                        int hit = 0;
                        const char *keys[] = {"wlan", "WCN", "cfg80211", "mac80211", "wifi", "WIFI"};
                        for (unsigned k = 0; k < sizeof(keys)/sizeof(keys[0]); k++)
                            if (strstr(msg, keys[k]) != NULL) hit = 1;
                        (void)low;
                        if (!hit) continue;
                        found++;
                        /* cắt prefix số dòng kmsg, lấy sau ';' */
                        int len = slen(msg);
                        while (len > 0 && (msg[len-1] == '\n' || msg[len-1] == '\r')) len--;
                        if (tl + len + 2 >= (int)sizeof(tail)) {
                            int keep = (int)sizeof(tail) / 2;
                            memmove(tail, tail + tl - keep, (size_t)keep);
                            tl = keep;
                        }
                        if (tl > 0 && tl + 2 < (int)sizeof(tail)) tail[tl++] = ' ';
                        int copy = len;
                        if (tl + copy >= (int)sizeof(tail)) copy = (int)sizeof(tail) - tl - 1;
                        if (copy > 0) {
                            memcpy(tail + tl, msg, (size_t)copy);
                            tl += copy;
                            tail[tl] = '\0';
                        }
                    }
                    close(fd);
                    if (!found || tl == 0) {
                        cat(resp, cap, "wifi: dmesg: no wlan/WCN/wifi lines (driver absent?)\n");
                    } else {
                        cat(resp, cap, "wifi: dmesg (");
                        cat_u(resp, cap, (unsigned)found);
                        cat(resp, cap, " lines): ");
                        cat(resp, cap, tail);
                        cat(resp, cap, "\n");
                    }
                    note("[    -    ] N qeaf: wifi probe answered");
                }
            }
            } /* sub == 0 */
#else
            cat(resp, cap, "wifi: available on the device only\n");
#endif
        } else if (eq(line, "sync")) {
            usb_gadget_set_cable(usb_gadget_cable());   /* republishes DEVINFO */
            cat(resp, cap, "sync: DEVINFO.TXT republished\n");
        } else if (starts(line, "usb ")) {
            const char *m = line + 4;
            enum usb_mode mode;
            if (eq(m, "charge")) mode = USB_MODE_CHARGE;
            else if (eq(m, "mtp")) mode = USB_MODE_MTP;
            else if (eq(m, "ptp")) mode = USB_MODE_PTP;
            else { cat(resp, cap, "usb: unknown mode (charge|mtp|ptp)\n"); continue; }
            if (usb_gadget_set_mode(mode) == 0)
                cat(resp, cap, "usb: mode set\n");
            else
                cat(resp, cap, "usb: refused (gadget not started?)\n");
        } else if (eq(line, "ls")) {
            cat(resp, cap,
                "HELP.TXT SUMMARY.TXT RESPONSE.TXT QEAFBOOT.LOG DEVINFO.TXT "
                "QEAF.CMD BOOTIMG.BIN SCREEN.RAW VQEAF UserData Install Themes\n");
        } else if (eq(line, "flash recovery BOOTIMG.BIN confirm")) {
            do_flash(vol, resp, cap);
        } else if (starts(line, "flash")) {
            cat(resp, cap,
                "flash: REFUSED - only 'flash recovery BOOTIMG.BIN confirm' "
                "is accepted\n");
            note("[    -    ] E qeaf: flash refused, unsafe verb");
        } else if (eq(line, "kmsg")) {
#ifdef __linux__
            int fd = open("/dev/kmsg", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
            if (fd < 0) {
                cat(resp, cap, "kmsg: cannot open /dev/kmsg\n");
            } else {
                char buf[4096];
                char tail[7168];
                int tl = 0;
                tail[0] = '\0';
                for (;;) {
                    ssize_t r = read(fd, buf, sizeof(buf) - 1);
                    char *msg, *h;
                    int len;
                    if (r <= 0) break;
                    buf[r] = '\0';
                    msg = strchr(buf, ';');
                    msg = msg ? msg + 1 : buf;
                    h = strstr(msg, "homo:");
                    if (!h) continue;
                    len = slen(h);
                    while (len > 0 && (h[len - 1] == '\n' || h[len - 1] == '\r'))
                        len--;
                    if (tl + len + 2 >= (int)sizeof(tail)) {
                        int keep = (int)sizeof(tail) / 2;
                        memmove(tail, tail + tl - keep, (size_t)keep);
                        tl = keep;
                    }
                    memcpy(tail + tl, h, (size_t)len);
                    tl += len;
                    tail[tl++] = '\n';
                    tail[tl] = '\0';
                }
                close(fd);
                if (tl == 0) cat(resp, cap, "kmsg: no homo: lines yet\n");
                else cat(resp, cap, tail);
            }
#else
            cat(resp, cap, "kmsg: available on the device only\n");
#endif
        } else if (eq(line, "snap")) {
            if (g_snapshot == NULL) {
                cat(resp, cap, "snap: unavailable (no scanout hook)\n");
            } else {
                int n = g_snapshot(vol);
                if (n > 0) {
                    cat(resp, cap, "snap: wrote SCREEN.RAW (");
                    cat_u(resp, cap, (unsigned)n);
                    cat(resp, cap, " bytes, RGB565 ");
                    cat_u(resp, cap, (unsigned)RESCUE_SCREEN_W);
                    cat(resp, cap, "x");
                    cat_u(resp, cap, (unsigned)RESCUE_SCREEN_H);
                    cat(resp, cap, ")\n");
                    note("[    -    ] N qeaf: snap wrote SCREEN.RAW");
                } else {
                    cat(resp, cap, "snap: failed (");
                    cat_u(resp, cap, (unsigned)(-n));
                    cat(resp, cap, ")\n");
                    note("[    -    ] E qeaf: snap failed");
                }
            }
        } else if (eq(line, "reboot") || starts(line, "reboot ")) {
#ifdef __linux__
            const char *arg = eq(line, "reboot") ? "system" : line + 7;
            if (eq(arg, "system")) {
                cat(resp, cap, "reboot: restarting to system\n");
                note("[    -    ] N qeaf: reboot -> system");
                put(vol, "RESPONSE.TXT", resp);
                put(vol, "QEAFBOOT.LOG", g_log);
                reboot(RB_AUTOBOOT);
            } else if (eq(arg, "recovery") || eq(arg, "bootloader")) {
                cat(resp, cap, "reboot: restarting to ");
                cat(resp, cap, arg);
                cat(resp, cap, "\n");
                note("[    -    ] N qeaf: reboot -> restart2");
                put(vol, "RESPONSE.TXT", resp);
                put(vol, "QEAFBOOT.LOG", g_log);
                syscall(SYS_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2,
                        LINUX_REBOOT_CMD_RESTART2, (void *)arg);
            } else {
                cat(resp, cap,
                    "reboot: unknown target (system|recovery|bootloader)\n");
                continue;
            }
            cat(resp, cap, "reboot: syscall returned (restart refused)\n");
#else
            cat(resp, cap, "reboot: available on the device only\n");
#endif
        } else {
            cat(resp, cap, "unknown verb\n");
        }
    }
}

void rescue_poll(struct fat16_volume *vol) {
    uint8_t cmd[CMD_CAP];
    char resp[RESP_CAP];
    int n;

    if (!vol) return;
    n = fat16_read_file(vol, "QEAF.CMD", cmd, sizeof(cmd) - 1);
    if (n <= 0) return;
    cmd[n] = '\0';
    if (n == g_last_len && memcmp(cmd, g_last, (size_t)n) == 0) return;
    if (n >= CMD_CAP) return;
    memcpy(g_last, cmd, (size_t)n);
    g_last_len = n;

    resp[0] = '\0';
    run_verbs(vol, (const char *)cmd, n, resp, sizeof(resp));
    put(vol, "RESPONSE.TXT", resp);
    put(vol, "QEAFBOOT.LOG", g_log);
    umos_logf("rescue: answered %d bytes of QEAF.CMD\n", n);
}
