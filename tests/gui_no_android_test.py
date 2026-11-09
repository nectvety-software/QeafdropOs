#!/usr/bin/env python3
"""Guard: the OS GUI must not contain the word "Android" (any case).

Requested on 2026-11-04: keep the System Service / "Dịch vụ hệ thống" folder,
but remove every "Android" label from the operating system's GUI. This scans the
runtime C sources (the code that actually draws the glass), not the build
scripts, docs or rescue-channel notes -- those still legitimately mention the
ANDROID! boot magic and the androidboot.console kernel cmdline.

Usage: python3 tests/gui_no_android_test.py
Exit 0 = clean, 1 = a GUI string still says Android.
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DIRS = ("apps", "gui", "init", "bsp", "hal", "core", "fs")
EXTRA = ("libc_stubs.c",)
NEEDLE = re.compile(r"android", re.IGNORECASE)
STRING_LITERAL = re.compile(r'"(?:\\.|[^"\\])*"')

# "ANDROID!" is the boot-header magic, a protocol token, not a brand label.
# The image patcher preserves it on purpose; the rescue channel validates it.
PROTOCOL_TOKEN = "ANDROID!"


def says_brand(text):
    """True when text carries the brand word outside the ANDROID! token."""
    for m in NEEDLE.finditer(text):
        if text[m.start():m.start() + len(PROTOCOL_TOKEN)] == PROTOCOL_TOKEN:
            continue
        return True
    return False


def source_files():
    for d in DIRS:
        base = os.path.join(ROOT, d)
        for dirpath, _, names in os.walk(base):
            for n in names:
                if n.endswith((".c", ".h")):
                    yield os.path.join(dirpath, n)
    for n in EXTRA:
        p = os.path.join(ROOT, n)
        if os.path.isfile(p):
            yield p


def main():
    files = sorted(set(source_files()))
    offenders = []
    strings_seen = 0
    for path in files:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            lines = f.readlines()
        for lineno, line in enumerate(lines, 1):
            for m in STRING_LITERAL.finditer(line):
                strings_seen += 1
                if says_brand(m.group(0)):
                    offenders.append((os.path.relpath(path, ROOT), lineno,
                                      m.group(0)))
            # Comments are checked too: a label described in a comment tends
            # to find its way back into the UI later.
            if says_brand(line) and all(
                    not says_brand(m.group(0))
                    for m in STRING_LITERAL.finditer(line)):
                offenders.append((os.path.relpath(path, ROOT), lineno,
                                  line.strip()))

    print("scanned %d runtime source files, %d string literals"
          % (len(files), strings_seen))
    # Negative controls: the allowlist must be exactly one token wide.
    if not says_brand("Boot the Android slot"):
        offenders.append(("tests/gui_no_android_test.py", 0,
                          "control: the brand word must still be caught"))
    if says_brand("checks ANDROID! magic"):
        offenders.append(("tests/gui_no_android_test.py", 0,
                          "control: the ANDROID! protocol token must pass"))
    if not says_brand("androidboot.console=ttyMSM0"):
        offenders.append(("tests/gui_no_android_test.py", 0,
                          "control: androidboot must still be caught"))
    if offenders:
        for rel, lineno, text in offenders:
            print("  FAIL %s:%d: %s" % (rel, lineno, text[:100]))
        print("\n%d offending line(s) - the GUI still says Android"
              % len(offenders))
        return 1
    print("  ok   no 'android' text anywhere in the OS runtime sources")
    print("\n1 check, 0 failures")
    return 0


if __name__ == "__main__":
    sys.exit(main())
