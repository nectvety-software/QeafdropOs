#include "wifi_core.h"
#include <stdbool.h>
#include <sys/stat.h>

static bool regular_file(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0;
}

static bool any_file(const char *a, const char *b, const char *c) {
    return regular_file(a) || regular_file(b) || regular_file(c);
}

bool wifi_core_present(void) {
    struct stat st;
    return stat("/sys/class/net/wlan0", &st) == 0 && S_ISDIR(st.st_mode);
}

bool wifi_core_fw_ready(void) {
    /* A directory called /lib/firmware is not firmware. WCN3990 needs the
     * ADSP image, WLAN modem image, board calibration and device MAC data.
     * Accept the common vendor and initramfs locations, but never placeholders. */
    bool adsp = any_file("/lib/firmware/adsp.mdt",
                         "/vendor/firmware/adsp.mdt",
                         "/vendor/firmware_mnt/image/adsp.mdt");
    bool wlan = any_file("/lib/firmware/wlanmdsp.mbn",
                         "/vendor/firmware/wlanmdsp.mbn",
                         "/vendor/firmware_mnt/image/wlanmdsp.mbn");
    bool cfg = regular_file("/lib/firmware/wlan/qca_cld/WCNSS_qcom_cfg.ini") ||
               regular_file("/vendor/firmware/WCNSS_qcom_cfg.ini") ||
               regular_file("/vendor/etc/wifi/WCNSS_qcom_cfg.ini");
    bool board = any_file("/lib/firmware/wlan/qca_cld/bdwlan.bin",
                          "/vendor/firmware/bdwlan.bin",
                          "/vendor/firmware_mnt/image/bdwlan.bin") ||
                 any_file("/lib/firmware/wlan/qca_cld/WCNSS_qcom_wlan_nv.bin",
                          "/vendor/firmware/WCNSS_qcom_wlan_nv.bin",
                          "/vendor/firmware_mnt/image/WCNSS_qcom_wlan_nv.bin");
    bool mac = any_file("/persist/wlan_mac.bin",
                        "/lib/firmware/wlan/qca_cld/wlan_mac.bin",
                        "/vendor/firmware/wlan_mac.bin") ||
                        regular_file("/vendor/firmware_mnt/image/wlan_mac.bin");
    return adsp && wlan && cfg && board && mac;
}

const char *wifi_core_state(void) {
    if (wifi_core_present()) return "IFACE_PRESENT: wlan0 detected; association not yet verified";
    if (!wifi_core_fw_ready())
        return "NO_FIRMWARE: WCN3990 ADSP/WLAN image, board calibration or MAC data missing";
    return "NO_IFACE: firmware files present but WLAN driver did not create wlan0";
}
