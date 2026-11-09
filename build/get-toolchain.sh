#!/bin/sh
# Fetch a self-contained cross toolchain for the target.
#
# musl rather than glibc is the whole reason the core can be static and tiny:
# glibc's NSS wants libnss_files.so at runtime even when everything else is
# linked in, which turns /etc/passwd lookups into a rootfs packaging puzzle.
#
# musl.cc only builds its crosses for Linux hosts, so on Windows the same libc
# comes from Zig, which carries musl sources for every target it advertises
# and needs no installer. 'zig cc -target aarch64-linux-musl' is then the
# compiler, linker and sysroot in one directory.
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=${OUT:-$ROOT/out}
DEST=${TOOLCHAIN_DIR:-$OUT/toolchain}
ARCH=${TARGET_ARCH:-aarch64}
ZIG_VERSION=${ZIG_VERSION:-0.13.0}
MAKE_VERSION=${MAKE_VERSION:-4.4.1}
URL=${TOOLCHAIN_URL:-https://musl.cc/${ARCH}-linux-musl-cross.tgz}

mkdir -p "$OUT" "$DEST"

case "$(uname -s)" in
    Linux|GNU|GNU/*) HOST=linux ;;
    MINGW*|MSYS*|CYGWIN*) HOST=windows ;;
    Darwin) HOST=macos ;;
    *) HOST=unknown ;;
esac

if [ "$HOST" = windows ]; then
    zigbin="$DEST/zig-windows-x86_64-$ZIG_VERSION/zig.exe"
    if [ ! -f "$zigbin" ]; then
        command -v curl >/dev/null 2>&1 || { echo "curl is required" >&2; exit 1; }
        command -v unzip >/dev/null 2>&1 || { echo "unzip is required" >&2; exit 1; }
        zip="$OUT/zig.zip"
        echo "toolchain: downloading zig $ZIG_VERSION (about 75 MB)"
        curl -fL --retry 3 \
            "https://ziglang.org/download/$ZIG_VERSION/zig-windows-x86_64-$ZIG_VERSION.zip" \
            -o "$zip"
        unzip -q -o "$zip" -d "$DEST"
        rm -f "$zip"
    fi
    # Git Bash ships no make, and busybox cannot be configured without it.
    # ezwinports build 4.4.1 needs no guile and runs from any PATH directory.
    makebin="$DEST/make/bin/make.exe"
    if [ ! -f "$makebin" ]; then
        zip="$OUT/make.zip"
        echo "toolchain: downloading make $MAKE_VERSION"
        curl -fL --retry 3 \
            "https://downloads.sourceforge.net/ezwinports/make-$MAKE_VERSION-without-guile-w32-bin.zip" \
            -o "$zip"
        unzip -q -o "$zip" -d "$DEST/make"
        rm -f "$zip"
    fi
    echo
    echo "toolchain: installed in $DEST"
    echo "toolchain: use it with (from the project root):"
    echo "toolchain:   export PATH=$DEST/make/bin:\$PATH"
    echo "toolchain:   make CC='$zigbin cc -target ${ARCH}-linux-musl' all"
    echo "toolchain: add SIZE='$zigbin objdump' to run 'make sizes'"
    exit 0
fi

tarball="$OUT/$(basename "$URL")"

if [ -x "$DEST/${ARCH}-linux-musl/bin/${ARCH}-linux-musl-gcc" ]; then
    echo "toolchain: already installed in $DEST"
    echo "toolchain: ${DEST}/${ARCH}-linux-musl/bin"
    exit 0
fi

command -v curl >/dev/null 2>&1 || { echo "curl is required" >&2; exit 1; }

echo "toolchain: downloading $URL"
if ! curl -fL --retry 3 "$URL" -o "$tarball"; then
    cat >&2 <<EOF
toolchain: download failed. Two ways forward:
  1. point TOOLCHAIN_URL at a mirror (the musl.cc build host keeps dated
     directories, and the stable name above occasionally moves)
  2. use your distro's package instead - it is glibc, not musl, so the
     binaries stay static but passwd lookups need /etc/nsswitch.conf:
        apt install gcc-aarch64-linux-gnu
        export CROSS_COMPILE=aarch64-linux-gnu-
EOF
    exit 1
fi

echo "toolchain: unpacking into $DEST"
tar -xf "$tarball" -C "$DEST" --strip-components=1
rm -f "$tarball"

BIN="$DEST/${ARCH}-linux-musl/bin"
[ -x "$BIN/${ARCH}-linux-musl-gcc" ] || {
    echo "toolchain: unpacked but the expected layout is not there:" >&2
    ls "$DEST" >&2
    exit 1
}

"$BIN/${ARCH}-linux-musl-gcc" --version | head -1
echo
echo "toolchain: add it to PATH with:"
echo "toolchain:   export PATH=$BIN:\$PATH"
echo "toolchain: then 'make all'"
