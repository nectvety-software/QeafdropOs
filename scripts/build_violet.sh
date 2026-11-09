#!/bin/sh
# SPDX-License-Identifier: MIT
# Build UMOS/violet for the phone (aarch64-linux-musl via Zig)
# Usage: sh scripts/build_violet.sh

set -e

ROOT=$(cd "$(dirname "$0")/.." && pwd)
ZIG="${ZIG:-zig}"

if ! command -v "$ZIG" >/dev/null 2>&1; then
    echo "build_violet: zig not found on PATH" >&2
    exit 1
fi

echo "build_violet: using $ZIG"
$ZIG version

make -C "$ROOT" all

# Do NOT pack qeafinit.elf into an ANDROID! image. That file is a userspace ELF;
# the resulting fake boot image was flashed to p77 on 2026-11-04 and caused the
# reboot loop. Only stage a real, validated install image when one is supplied.
if [ -n "${P77_IMAGE:-}" ]; then
    make -C "$ROOT" bootimg P77_IMAGE="$P77_IMAGE"
    echo "build_violet: staged validated p77 image -> out/boot-qeaf-violet.img"
else
    echo "build_violet: no p77 image produced (by design); p77 untouched"
    echo "build_violet: P77_IMAGE=<validated .img> to stage one, then"
    echo "build_violet: python3 scripts/prepare_rescue_flash.py --volume <LUN> --commit"
fi

echo "build_violet: done -> out/qeafinit.elf"
