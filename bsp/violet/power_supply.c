/* SPDX-License-Identifier: MIT */
/* bsp/violet/power_supply.c - violet's power_supply nodes, read honestly.

 * The nodes this board exposes (measured on the m46 boot through the rescue
 * channel): battery/{present,status,capacity} and the supply-side
 * usb/online, dc/online. Names are the kernel's, values are parsed, not
 * pattern-matched, so a capacity that changed between reads is a capacity
 * that changed on screen.
 */

#include "hal/include/battery.h"
#include "core/sysio.h"

static char g_root[96] = "/sys";

void battery_set_sysfs_root(const char *root) {
    int i = 0;
    if (!root) return;
    while (i + 1 < (int)sizeof(g_root) && root[i]) {
        g_root[i] = root[i];
        i++;
    }
    g_root[i] = '\0';
}

static void node(char *buf, int cap, const char *supply, const char *attr) {
    const char *base = g_root;
    int n = 0;
    static const char mid[] = "/class/power_supply/";
    const char *s;

    buf[0] = '\0';
    for (s = base; *s && n + 1 < cap; s++) buf[n++] = *s;
    for (s = mid; *s && n + 1 < cap; s++) buf[n++] = *s;
    for (s = supply; *s && n + 1 < cap; s++) buf[n++] = *s;
    buf[n++] = '/';
    for (s = attr; *s && n + 1 < cap; s++) buf[n++] = *s;
    buf[n < cap ? n : cap - 1] = '\0';
}

static void trim(char *s) {
    int n = 0;
    while (s[n]) n++;
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' '))
        s[--n] = '\0';
}

static int starts_with(const char *s, const char *p) {
    while (*p) {
        if (*s != *p) return 0;
        s++; p++;
    }
    return 1;
}

int battery_read(struct battery_state *out) {
    char path[160];
    char text[32];
    int n;
    int online;

    if (!out) return -1;

    node(path, (int)sizeof(path), "battery", "status");
    n = sysio_read_file(path, text, (int)sizeof(text));
    if (n < 0 || text[0] == '\0') return -1;
    trim(text);

    int capacity = -1;
    node(path, (int)sizeof(path), "battery", "capacity");
    if (sysio_read_int(path, &capacity) < 0) return -1;
    if (capacity < 0 || capacity > 100) return -1;

    int plugged = 0;
    node(path, (int)sizeof(path), "usb", "online");
    if (sysio_read_int(path, &online) == 0) plugged = online != 0;
    else {
        node(path, (int)sizeof(path), "dc", "online");
        if (sysio_read_int(path, &online) == 0) plugged = online != 0;
    }

    out->capacity = capacity;
    out->plugged = plugged;
    out->charging = starts_with(text, "Charging");
    int i = 0;
    for (; i + 1 < (int)sizeof(out->status) && text[i]; i++) out->status[i] = text[i];
    out->status[i] = '\0';
    return 0;
}
