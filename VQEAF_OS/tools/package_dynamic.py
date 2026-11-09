#!/usr/bin/env python3
"""Package the dynamic PIE homo-init for violet (experimental).

Same pipeline as package_violet.py, but INIT is homo-init-dynamic and the
stage also carries glibc arm64 libs from out/glibc-extract so the
interpreter /lib/ld-linux-aarch64.so.1 and NEEDED libs resolve.

Usage:
  python homo/scripts/package_dynamic.py
"""
import hashlib, os, shutil, subprocess, sys

# resolve like package_violet.py
this = os.path.dirname(os.path.abspath(__file__))
HOMO = os.path.dirname(this)
QEAF = os.path.dirname(HOMO)
BUILD = os.path.join(HOMO, "build", "violet")
STAGE = os.path.join(BUILD, "stage-dynamic")
RAMDISK = os.path.join(BUILD, "qeaf-initramfs.cpio.gz")
IMAGE = os.path.join(QEAF, "out", "boot-qeafdropos-violet.img")
REFERENCE = os.path.join(QEAF, "out", "reference", "stock-violet", "boot.img")
MK_INITRAMFS = os.path.join(QEAF, "scripts", "mk_initramfs.py")
MK_BOOTIMG = os.path.join(QEAF, "scripts", "mk_bootimg_violet.py")
INIT = os.path.join(BUILD, "homo-init-dynamic")
GLIBC_SRC = os.path.join(QEAF, "out", "glibc-extract", "usr", "lib", "aarch64-linux-gnu")
ASSETS = os.path.join(HOMO, "custom", "assets")

def tree_size(root):
    total = 0
    for dp, _d, fs in os.walk(root):
        for f in fs:
            total += os.path.getsize(os.path.join(dp, f))
    return total

shutil.rmtree(STAGE, ignore_errors=True)
os.makedirs(STAGE)
shutil.copy2(INIT, os.path.join(STAGE, "init"))
os.chmod(os.path.join(STAGE, "init"), 0o755)
os.makedirs(os.path.join(STAGE, "assets"))
shutil.copytree(os.path.join(ASSETS, "ios"), os.path.join(STAGE, "assets", "ios"))
for d in ("proc", "sys", "dev", "run", "tmp", "mnt", "config", "var", os.path.join("var", "lib"), os.path.join("var", "lib", "hachimi"), "lib", "usr"):
    os.makedirs(os.path.join(STAGE, d), exist_ok=True)
# glibc: interp + NEEDED
libs = ["ld-linux-aarch64.so.1", "libc.so.6", "libm.so.6", "libpthread.so.0", "libdl.so.2"]
for lib in libs:
    src = os.path.join(GLIBC_SRC, lib)
    assert os.path.isfile(src), f"missing {src}"
    shutil.copy2(src, os.path.join(STAGE, "lib", lib))
    shutil.copy2(src, os.path.join(STAGE, "usr", lib))
print("staged libs", libs)
print("staged", tree_size(STAGE), "bytes")
links = os.path.join(BUILD, "stage-symlinks.txt")
subprocess.check_call([sys.executable, MK_INITRAMFS, "--stage", STAGE, "--links", links, "--out", BUILD])
print("ramdisk", os.path.getsize(RAMDISK))
# rename to expected name for mk_bootimg
subprocess.check_call([sys.executable, MK_BOOTIMG, "--reference", REFERENCE, "--ramdisk", RAMDISK, "--output", IMAGE])
print("image", os.path.getsize(IMAGE))
print("sha256", hashlib.sha256(open(IMAGE, "rb").read()).hexdigest())
