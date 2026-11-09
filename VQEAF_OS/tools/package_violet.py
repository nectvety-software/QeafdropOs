#!/usr/bin/env python3
"""Package QeafdropOs for the Redmi Note 7 Pro (violet) as a flashable boot image.

Pipeline, in order:

    build/violet/homo-init            static aarch64-musl PID 1 (build_violet.py)
        + custom/assets/ios           the desktop's PNG/TTF assets
        -> build/violet/stage         the initramfs root filesystem
        -> qeaf-initramfs.cpio(.gz)   newc archive + gzip, via QeafdropOs
        -> boot-qeafdropos-violet.img stock violet kernel + our ramdisk

The kernel and DTB are copied byte for byte from QeafdropOs's dump of the real
violet boot partition, so this image boots the same way the phone already does;
only the ramdisk (and therefore /init) changes. The single image lands in
QeafdropOs/out so validate_bootimg.py and prepare_rescue_flash.py see it directly.

    python scripts/package_violet.py
    python scripts/package_violet.py --clean
    python scripts/package_violet.py --with-maps   # see the size guard below

`--with-maps` is off by default: custom/assets/maps is ~97 MiB of Beijing tiles,
which cannot fit in p77's 64 MiB. The Maps app reports the missing directory
instead of taking the whole image down.
"""
import argparse
import hashlib
import os
import shutil
import struct
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build_violet as bv  # noqa: E402  (shares ROOT / QEAF / TARGET / BUILD)

ROOT = bv.ROOT
QEAF = bv.QEAF
BUILD = bv.BUILD
INIT = bv.TARGET
STAGE = os.path.join(BUILD, "stage")
ASSETS = os.path.join(ROOT, "custom", "assets")
MAPS = os.path.join(ASSETS, "maps")
RAMDISK = os.path.join(BUILD, "qeaf-initramfs.cpio.gz")
IMAGE = os.path.join(QEAF, "out", "boot-qeafdropos-violet.img")
REFERENCE = os.path.join(QEAF, "out", "reference", "stock-violet", "boot.img")
MK_INITRAMFS = os.path.join(QEAF, "scripts", "mk_initramfs.py")
MK_BOOTIMG = os.path.join(QEAF, "scripts", "mk_bootimg_violet.py")

# /dev/mmcblk0p77, the partition the phone boots with `fastboot reboot recovery`.
P77_SIZE = 67108864
# The kernel decompresses the ramdisk into RAM before /init runs; leave room for
# the kernel, the 32 MiB LUN image and the desktop heap.
MAX_RAMDISK = 40 << 20
MAX_STAGE = 48 << 20

EM_AARCH64 = 0xB7


def fail(message):
    sys.stderr.write("package_violet: %s\n" % message)
    raise SystemExit(1)


def check_init():
    if not os.path.isfile(INIT):
        fail("%s is missing; run `python scripts/build_violet.py` first" % INIT)
    with open(INIT, "rb") as handle:
        head = handle.read(20)
    if head[:4] != b"\x7fELF" or head[4] != 2:
        fail("%s is not a 64-bit ELF" % INIT)
    machine = struct.unpack_from("<H", head, 18)[0]
    if machine != EM_AARCH64:
        fail("%s is built for machine 0x%04x, not aarch64" % (INIT, machine))


def copy_tree(source, target):
    if not os.path.isdir(source):
        fail("%s is missing" % source)
    shutil.copytree(source, target)


def stage_root(with_maps):
    shutil.rmtree(STAGE, ignore_errors=True)
    os.makedirs(STAGE)

    shutil.copy2(INIT, os.path.join(STAGE, "init"))
    os.chmod(os.path.join(STAGE, "init"), 0o755)

    os.makedirs(os.path.join(STAGE, "assets"))
    copy_tree(os.path.join(ASSETS, "ios"), os.path.join(STAGE, "assets", "ios"))

    # Persist .qeapp bundles in the initramfs so they survive reboot (the
    # QEAFRESCUE LUN is RAM-only and loses F:\Apps on reboot). They land at
    # /apps/*.qeapp on the device and are browsable via Files -> /apps.
    apps_dir = os.path.join(STAGE, "apps")
    os.makedirs(apps_dir, exist_ok=True)
    apps_src = os.path.join(QEAF, "out", "apps")
    if os.path.isdir(apps_src):
        for name in sorted(os.listdir(apps_src)):
            if name.endswith(".qeapp"):
                shutil.copy2(os.path.join(apps_src, name),
                             os.path.join(apps_dir, name))
    installer_src = os.path.join(QEAF, "out", "installer.qeapp")
    if os.path.isfile(installer_src):
        shutil.copy2(installer_src,
                     os.path.join(apps_dir, "installer.qeapp"))

    # Wi-Fi/Bluetooth core: stage WCN stock firmware crumbs (vendor.img
    # etc/wifi/WCNSS_qcom_cfg.ini) so /lib/firmware/wlan/qca_cld exists.
    # The heavy WCN blob lives in modem p82 + persist p74 (wlan_mac.bin);
    # those are not in this image yet, so wifi_core honestly reports
    # NO_FIRMWARE until they are added.
    fw_src = os.path.join(QEAF, "out", "stock-firmware", "etc", "wifi", "WCNSS_qcom_cfg.ini")
    if os.path.isfile(fw_src):
        fw_dir = os.path.join(STAGE, "lib", "firmware", "wlan", "qca_cld")
        os.makedirs(fw_dir, exist_ok=True)
        shutil.copy2(fw_src, os.path.join(fw_dir, "WCNSS_qcom_cfg.ini"))
        # wlan_mac.bin is a symlink to persist on stock; stage an empty
        # placeholder so the path exists (kernel will still fail checksum
        # until real persist bytes are staged).
        open(os.path.join(fw_dir, "wlan_mac.bin"), "wb").close()

    if with_maps:
        copy_tree(MAPS, os.path.join(STAGE, "assets", "maps"))

    # Created here so the cpio tree is complete; vio_setup_environment() mounts
    # over them at boot. /config is a real directory because configfs cannot be
    # mounted on a path that does not exist.
    for directory in ("proc", "sys", "dev", "run", "tmp", "mnt", "config",
                      "var", os.path.join("var", "lib"),
                      os.path.join("var", "lib", "hachimi")):
        os.makedirs(os.path.join(STAGE, directory), exist_ok=True)

    # No applets, no busybox and no symlinks: /init is the whole userspace.
    with open(os.path.join(BUILD, "stage-symlinks.txt"), "w", encoding="utf-8") as handle:
        handle.write("# Homo OS violet has no busybox applets to link.\n")


def tree_size(root):
    total = 0
    for dirpath, _dirs, files in os.walk(root):
        for name in files:
            total += os.path.getsize(os.path.join(dirpath, name))
    return total


def largest_files(root, count=5):
    entries = []
    for dirpath, _dirs, files in os.walk(root):
        for name in files:
            path = os.path.join(dirpath, name)
            entries.append((os.path.getsize(path),
                            os.path.relpath(path, root).replace(os.sep, "/")))
    entries.sort(reverse=True)
    return entries[:count]


def run(command, what):
    result = subprocess.run([sys.executable] + command, cwd=ROOT,
                            capture_output=True, text=True)
    if result.stdout:
        sys.stdout.write(result.stdout)
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        fail("%s failed (exit %d)" % (what, result.returncode))
    return result


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("--with-maps", action="store_true",
                        help="include the ~97 MiB offline map tiles (will not fit p77)")
    args = parser.parse_args(argv)

    for path in (REFERENCE, MK_INITRAMFS, MK_BOOTIMG):
        if not os.path.isfile(path):
            fail("missing QeafdropOs dependency: %s (set QEAFDROPOS=...)" % path)
    check_init()

    print("package_violet: staging %s" % STAGE)
    stage_root(args.with_maps)
    staged = tree_size(STAGE)
    if staged > MAX_STAGE:
        fail("staged tree is %d bytes, over the %d-byte budget" % (staged, MAX_STAGE))
    print("package_violet: %d bytes staged (%s)"
          % (staged, "with maps" if args.with_maps else "no maps"))
    for size, name in largest_files(STAGE):
        print("  %9d  %s" % (size, name))

    run([MK_INITRAMFS,
         "--stage", STAGE,
         "--links", os.path.join(BUILD, "stage-symlinks.txt"),
         "--out", BUILD], "mk_initramfs")
    if not os.path.isfile(RAMDISK):
        fail("mk_initramfs did not produce %s" % RAMDISK)
    ramdisk = os.path.getsize(RAMDISK)
    if ramdisk > MAX_RAMDISK:
        fail("ramdisk is %d bytes, over the %d-byte boot budget"
             % (ramdisk, MAX_RAMDISK))

    run([MK_BOOTIMG,
         "--reference", REFERENCE,
         "--ramdisk", RAMDISK,
         "--output", IMAGE], "mk_bootimg_violet")
    if not os.path.isfile(IMAGE):
        fail("mk_bootimg_violet did not produce %s" % IMAGE)

    size = os.path.getsize(IMAGE)
    if size > P77_SIZE:
        fail("image is %d bytes, larger than p77 (%d bytes)" % (size, P77_SIZE))

    print("")
    print("package_violet: %s" % IMAGE)
    print("  image    %d bytes (p77 is %d, %.0f%% used)"
          % (size, P77_SIZE, 100.0 * size / P77_SIZE))
    print("  ramdisk  %d bytes gzipped" % ramdisk)
    print("  sha256   %s" % sha256(IMAGE))
    print("")
    print("Flash it to p77 (never p76/boot):")
    print("  python \"%s\" --image \"%s\" --volume <QEAFRESCUE drive>"
          % (os.path.join(QEAF, "scripts", "prepare_rescue_flash.py"), IMAGE))
    print("  # then add --commit to the same command to agree to the write")
    return 0


if __name__ == "__main__":
    sys.exit(main())
