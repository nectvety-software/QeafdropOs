#include "framework/core/bluetooth_core.h"
#include "framework/core/wifi_core.h"
#include <stdio.h>
#include <string.h>

/* Host gate for the two radio state machines in homo/framework/core.
 *
 * The device paths (/sys/class/net/wlan0, /sys/class/bluetooth/hci0) do not
 * exist on the host, so the check cannot assert a particular state. It does
 * assert the invariant that matters: the state text may only claim a present
 * interface when the same probe that feeds it reports the interface present.
 * That catches the old bug where a bare directory was treated as a live
 * controller and the UI printed a ready state for hardware that was absent. */
static int check_wifi(void) {
    int present = wifi_core_present() ? 1 : 0;
    int fw = wifi_core_fw_ready() ? 1 : 0;
    const char *state = wifi_core_state();
    int claims_present = strncmp(state, "IFACE_PRESENT", 13) == 0;
    printf("wifi present=%d fw=%d state=%s\n", present, fw, state);
    if (present != claims_present) {
        printf("radio_state_smoke: FAIL wifi state disagrees with probe\n");
        return 1;
    }
    if (!present && strncmp(state, "NO_FIRMWARE", 11) != 0 &&
        strncmp(state, "NO_IFACE", 8) != 0) {
        printf("radio_state_smoke: FAIL wifi absent without a NO_* state\n");
        return 1;
    }
    return 0;
}

static int check_bluetooth(void) {
    int present = bt_core_present() ? 1 : 0;
    const char *state = bt_core_state();
    int claims_present = strncmp(state, "HCI_PRESENT", 11) == 0;
    printf("bt present=%d state=%s\n", present, state);
    if (present != claims_present) {
        printf("radio_state_smoke: FAIL bt state disagrees with probe\n");
        return 1;
    }
    if (!present && strncmp(state, "NO_HCI", 6) != 0) {
        printf("radio_state_smoke: FAIL bt absent without a NO_HCI state\n");
        return 1;
    }
    return 0;
}

int main(void) {
    int failures = check_wifi() + check_bluetooth();
    printf("radio_state_smoke: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
