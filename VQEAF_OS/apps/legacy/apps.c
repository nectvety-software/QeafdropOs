/* SPDX-License-Identifier: MIT */
/* apps/apps.c - Register all apps */

#include "apps.h"
#include "gui/window_manager.h"
#include "apps/launcher/home.h"
#include "apps/lock/lock_screen.h"
#include "apps/settings/settings.h"
#include "apps/messages/messages.h"
#include "apps/gallery/gallery.h"
#include "apps/browser/browser.h"
#include "apps/recovery/recovery.h"
#include "apps/fastboot/fastboot.h"
#include "apps/usbui/usbui.h"

void umos_apps_register_all(void) {
    home_init();
    lock_init();
    settings_init();
    messages_init();
    gallery_init();
    browser_init();
    recovery_init();
    fastboot_init();
    usbui_init();
}
