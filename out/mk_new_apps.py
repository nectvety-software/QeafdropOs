import os, json, shutil, pathlib
root = pathlib.Path("out/new-apps")
if root.exists():
    shutil.rmtree(root)
root.mkdir(parents=True)
apps = [
    ("bootui", "com.qeaf.bootui", "Bootui", "bootui.c", "browser.png"),
    ("browser", "com.qeaf.browser", "Browser", "browser.c", "browser.png"),
    ("fastboot", "com.qeaf.fastboot", "Fastboot", "fastboot.c", "wifi.png"),
    ("gallery", "com.qeaf.gallery", "Gallery", "gallery.c", "files.png"),
    ("installer", "com.qeaf.installer", "QeafdropOs Installer", "installer.c", "settings.png"),
    ("keyboard", "com.qeaf.keyboard", "Keyboard", "keyboard.c", "wifi.png"),
    ("launcher", "com.qeaf.launcher", "Launcher", "home.c", "files.png"),
    ("lock", "com.qeaf.lock", "Lock", "lock_screen.c", "settings_about.png"),
    ("messages", "com.qeaf.messages", "Messages", "messages.c", "messages.png"),
    ("recovery", "com.qeaf.recovery", "Recovery", "recovery.c", "wifi.png"),
    ("settings", "com.qeaf.settings", "Settings", "settings.c", "settings.png"),
    ("usbui", "com.qeaf.usbui", "Usbui", "usbui.c", "files.png"),
]
src_template = """/* SPDX-License-Identifier: MIT */
/* {name} app - QeafdropOs .qeapp (Homo LVGL) */
#include "lvgl.h"

static lv_obj_t * create(lv_obj_t * parent)
{{
    lv_obj_t * label = lv_label_create(parent);
    lv_label_set_text(label, "{name}");
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    return parent;
}}

const void * q_create = create;
"""
for folder, id_, name, entry_c, icon_src in apps:
    base = root / folder
    (base / "src").mkdir(parents=True)
    (base / "assets").mkdir(parents=True)
    manifest = {"id": id_, "name": name, "version": "1.0.0", "entry": f"src/{entry_c}", "min_os": "540x1170", "kind": "app"}
    (base / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    (base / "src" / entry_c).write_text(src_template.format(name=name), encoding="utf-8")
    (base / "README.md").write_text(f"# {name}\n\nQeafdropOs .qeapp built with make_qeapp.py.\n", encoding="utf-8")
    # icon
    src_icon = pathlib.Path("homo/custom/assets/ios") / icon_src
    if src_icon.is_file():
        shutil.copy2(src_icon, base / "assets" / "icon.png")
    else:
        shutil.copy2(pathlib.Path("homo/custom/assets/ios/settings.png"), base / "assets" / "icon.png")
    print("created", folder)
print("done")
