#include "wifi_core.h"
#include <dirent.h>
#include <sys/stat.h>

static bool dir_exists(const char *p) {
    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

bool wifi_core_present(void) {
    return dir_exists("/sys/class/net/wlan0");
}

bool wifi_core_fw_ready(void) {
    return dir_exists("/lib/firmware/wlan/qca_cld") ||
           dir_exists("/vendor/firmware/wlan/qca_cld") ||
           dir_exists("/vendor/firmware") ||
           dir_exists("/lib/firmware");
}

const char *wifi_core_state(void) {
    if (wifi_core_present()) return "CONNECTED";
    if (!wifi_core_fw_ready()) return "NO_FIRMWARE: modem p82 + persist p74 not staged";
    return "NO_IFACE: firmware present but wlan0 not created";
}
