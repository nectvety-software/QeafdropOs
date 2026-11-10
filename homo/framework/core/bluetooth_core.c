#include "bluetooth_core.h"
#include <sys/stat.h>

bool bt_core_present(void) {
    struct stat st;
    /* The class directory exists even with no controller. Only hci0 proves
     * that the kernel registered a Bluetooth HCI device. */
    return stat("/sys/class/bluetooth/hci0", &st) == 0 && S_ISDIR(st.st_mode);
}

const char *bt_core_state(void) {
    return bt_core_present() ?
        "HCI_PRESENT: controller detected; pairing stack still required" :
        "NO_HCI: WCN3990 firmware/transport did not register a controller";
}
