#!/usr/bin/env python3
"""Cross-compile the QeafdropOs desktop for the Redmi Note 7 Pro (violet).

This replaces `make` on Windows. It compiles LVGL, the Homo-derived runtime,
the violet platform layer and the QeafdropOs rescue files with the same Zig
toolchain QeafdropOs uses, then links one static aarch64-musl PID 1 binary.

    python scripts/build_violet.py            # incremental build
    python scripts/build_violet.py --clean    # rebuild every object
    python scripts/build_violet.py --verbose  # echo each compiler command

This tree (homo/) is vendored inside QeafdropOs; the parent checkout is the
single source of fs/fat16.c, fs/storage.c, fs/rescue.c, core/sysio.c and
core/log.c, compiled in so the QEAFRESCUE drive behaves exactly like the one
tested on the phone. There is one OS, one image, one version: QeafdropOs.
"""
import argparse
import hashlib
import os
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor, as_completed

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def _default_qeaf():
    """Locate the QeafdropOs checkout that owns the shared rescue channel.

    Vendored layout first: homo/ lives inside QeafdropOs, so the parent of
    ROOT is the checkout itself when it carries fs/rescue.c and core/log.c.
    Fall back to the sibling/known-drive layouts for a standalone Homo tree.
    """
    parent = os.path.dirname(ROOT)
    candidates = [
        parent,
        os.path.join(parent, "QeafivelsOfficial", "QeafdropOs"),
        os.path.join("D:\\", "QeafivelsOfficial", "QeafdropOs"),
        os.path.join(os.path.expanduser("~"), "QeafivelsOfficial", "QeafdropOs"),
    ]
    for candidate in candidates:
        if os.path.isfile(os.path.join(candidate, "fs", "rescue.c")) and \
           os.path.isfile(os.path.join(candidate, "core", "log.c")):
            return candidate
    return candidates[0]


QEAF = os.environ.get("QEAFDROPOS") or _default_qeaf()
DEFAULT_ZIG = os.path.join(
    QEAF, "out", "toolchain", "zig-windows-x86_64-0.13.0", "zig.exe"
)
ZIG = os.environ.get("ZIG", DEFAULT_ZIG)

BUILD = os.path.join(ROOT, "build", "violet")
OBJ = os.path.join(BUILD, "obj")
TARGET = os.path.join(BUILD, "homo-init")

CFLAGS = [
    "-target", "aarch64-linux-musl",
    "-O2",
    "-std=gnu99",
    "-Wall",
    "-Wextra",
    "-Wno-unused-parameter",
    "-fno-strict-aliasing",
    "-D_GNU_SOURCE",
    "-DHACHIMI_USE_NEON=0",
    "-DHACHIMI_USE_PXP=0",
    "-DLV_CONF_INCLUDE_SIMPLE",
    "-I" + ROOT,
    "-I" + os.path.join(ROOT, "custom"),
    "-I" + os.path.join(ROOT, "platform", "lvgl"),
    "-I" + os.path.join(ROOT, "platform", "lvgl", "src"),
    "-I" + os.path.join(ROOT, "platform", "violet"),
    "-I" + QEAF,
]

LDFLAGS = ["-static", "-pthread", "-lm"]


def runtime_sources():
    listing = os.path.join(ROOT, "cmake", "hachimi_runtime_sources.list")
    sources = []
    with open(listing, encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            sources.append(os.path.join(ROOT, line.replace("/", os.sep)))
    sources.append(os.path.join(ROOT, "platform", "linux", "service_job.c"))
    sources.append(os.path.join(ROOT, "platform", "linux", "led_backend.c"))
    return sources


def lvgl_sources():
    base = os.path.join(ROOT, "platform", "lvgl", "src")
    found = []
    for dirpath, _dirs, files in os.walk(base):
        for name in files:
            if name.endswith(".c"):
                found.append(os.path.join(dirpath, name))
    return sorted(found)


def violet_sources():
    base = os.path.join(ROOT, "platform", "violet")
    names = [
        "devnodes.c", "panel_scale.c", "drm_kms.c", "violet_input.c",
        "usb_lun.c", "display_hal.c", "main_violet.c", "violet_power.c",
    ]
    return [os.path.join(base, name) for name in names]


def qeaf_sources():
    names = [
        os.path.join("fs", "fat16.c"),
        os.path.join("fs", "storage.c"),
        os.path.join("fs", "rescue.c"),
        os.path.join("core", "log.c"),
        os.path.join("core", "sysio.c"),
        os.path.join("core", "prefs.c"),
        os.path.join("core", "i18n.c"),
    ]
    return [os.path.join(QEAF, name) for name in names]


def all_sources():
    seen = set()
    result = []
    for path in lvgl_sources() + runtime_sources() + violet_sources() + qeaf_sources():
        path = os.path.abspath(path)
        if path in seen:
            continue
        if not os.path.isfile(path):
            raise SystemExit("build_violet: missing source %s" % path)
        seen.add(path)
        result.append(path)
    return result


def object_for(source):
    digest = hashlib.sha1(source.encode("utf-8")).hexdigest()[:10]
    stem = os.path.splitext(os.path.basename(source))[0]
    return os.path.join(OBJ, "%s-%s.o" % (stem, digest))


def stamp_mtime():
    paths = [
        os.path.join(ROOT, "config", "lv_conf_common.h"),
        os.path.join(ROOT, "platform", "violet", "lv_conf.h"),
    ]
    for base in (
        os.path.join(ROOT, "platform", "violet"),
        os.path.join(QEAF, "fs"),
        os.path.join(QEAF, "core"),
        os.path.join(QEAF, "hal", "include"),
    ):
        if not os.path.isdir(base):
            continue
        for name in os.listdir(base):
            if name.endswith(".h"):
                paths.append(os.path.join(base, name))
    newest = 0.0
    for path in paths:
        try:
            newest = max(newest, os.path.getmtime(path))
        except OSError:
            pass
    return newest


def flags_stamp():
    text = "\n".join(CFLAGS + LDFLAGS)
    return hashlib.sha1(text.encode("utf-8")).hexdigest()


def compile_one(source, verbose, headers_mtime):
    target = object_for(source)
    try:
        if os.path.getmtime(target) >= max(os.path.getmtime(source), headers_mtime):
            return target, None
    except OSError:
        pass
    command = [ZIG, "cc"] + CFLAGS + ["-c", source, "-o", target]
    if verbose:
        print(" ".join(command))
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        return target, "command: %s\n%s\n%s" % (" ".join(command),
                                                result.stdout, result.stderr)
    return target, None


def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("--clean", action="store_true")
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    args = parser.parse_args(argv)

    if not os.path.isfile(ZIG):
        raise SystemExit("build_violet: zig not found at %s (set ZIG=...)" % ZIG)
    if not os.path.isdir(QEAF):
        raise SystemExit("build_violet: QeafdropOs not found at %s" % QEAF)

    os.makedirs(OBJ, exist_ok=True)

    stamp_path = os.path.join(BUILD, "flags.sha1")
    current = flags_stamp()
    if args.clean:
        shutil.rmtree(BUILD, ignore_errors=True)
        os.makedirs(OBJ, exist_ok=True)
    elif os.path.isfile(stamp_path):
        with open(stamp_path, encoding="ascii") as handle:
            if handle.read().strip() != current:
                print("build_violet: flags changed, rebuilding all objects")
                shutil.rmtree(OBJ, ignore_errors=True)
                os.makedirs(OBJ, exist_ok=True)
    with open(stamp_path, "w", encoding="ascii") as handle:
        handle.write(current + "\n")

    sources = all_sources()
    headers_mtime = stamp_mtime()
    print("build_violet: %d sources -> %s" % (len(sources), BUILD))

    failures = []
    objects = []
    with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        futures = [pool.submit(compile_one, source, args.verbose, headers_mtime)
                   for source in sources]
        done = 0
        for future in as_completed(futures):
            target, error = future.result()
            done += 1
            if error:
                failures.append(error)
            else:
                objects.append(target)
            if done % 50 == 0 or done == len(sources):
                print("  compiled %d/%d" % (done, len(sources)))

    if failures:
        for error in failures[:5]:
            print(error, file=sys.stderr)
        raise SystemExit("build_violet: %d source(s) failed" % len(failures))

    # Windows caps a command line at 32 KiB; 460+ object paths do not fit, so
    # the link arguments go through a response file (zig cc expands @file).
    response = os.path.join(BUILD, "link.rsp")
    with open(response, "w", encoding="utf-8") as handle:
        for obj in sorted(objects):
            handle.write(obj + "\n")
        for flag in LDFLAGS:
            handle.write(flag + "\n")
        handle.write("-o\n" + TARGET + "\n")
    # The target must be repeated at link time: without it zig cc assumes the
    # host triple and hands the objects to lld-link.
    command = [ZIG, "cc", "-target", "aarch64-linux-musl", "@" + response]
    if args.verbose:
        print(" ".join(command))
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        sys.stderr.write(result.stdout)
        sys.stderr.write(result.stderr)
        raise SystemExit("build_violet: link failed")
    print("build_violet: %s (%d bytes)" % (TARGET, os.path.getsize(TARGET)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
