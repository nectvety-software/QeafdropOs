#include "bluetooth_core.h"
#include <dirent.h>
#include <sys/stat.h>

static bool dir_exists(const char *p) {
    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

bool bt_core_present(void) {
    if (dir_exists("/sys/class/bluetooth")) {
        DIR *d = opendir("/sys/class/bluetooth");
        if (!d) return false;
        int n = 0;
        while (readdir(d)) n++;
        closedir(d);
        return n > 2;
    }
    return false;
}

const char *bt_core_state(void) {
    return bt_core_present() ? "READY" : "NO_HCI: needs WCN firmware (same as wifi)";
}
