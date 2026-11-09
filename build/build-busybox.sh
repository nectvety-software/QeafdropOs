#!/bin/sh
# Build a static busybox with exactly the applets VQEAF OS calls.
#
# The default defconfig would add ~1 MB of applets nobody uses; starting from
# allnoconfig and switching on the list in build/mk-stage.sh keeps the whole
# userland claim honest.
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=${OUT:-$ROOT/out}
SRC=${BUSYBOX_SRC:-$OUT/src}
VER=${BUSYBOX_VER:-1.37.0}
CROSS_COMPILE=${CROSS_COMPILE:-aarch64-linux-musl-}
TARBALL="$SRC/busybox-$VER.tar.bz2"

APPLETS="
SH ASH GETTY LOGIN MOUNT UMOUNT SWAPON SWAPOFF MKSWAP MOUNTPOINT
MODPROBE INSMOD RMMOD LSMOD
CAT LS RM MV CP MKDIR RMDIR LN READLINK CHMOD CHOWN TOUCH STAT FIND XARGS
HEAD TAIL WC GREP SED CUT TR SORT UNIQ ECHO PRINTF AWK BASENAME
DATE UPTIME FREE PS KILL KILLALL SLEEP SYNC CLEAR STTY
DMESG SYSLOGD KLOGD MKNOD TRUE FALSE TEST EXPR YES
HEXDUMP OD UNAME HOSTNAME RESET WATCH ZCAT
"

die() { echo "busybox: $*" >&2; exit 1; }

# kbuild has no Windows host support of any kind, measured rather than
# assumed: with a working cross compiler the tree still dies in
# scripts/kconfig, which builds mconf (needs sys/ioctl.h and curses), and
# kbuild execs its host programs without a .exe suffix, so even a compiled
# 'conf' cannot be run from the makefile. Configuring busybox therefore needs
# a POSIX host - Linux, WSL or a container - even when the target compiler is
# available here. Drop a static aarch64 binary at out/bin/busybox (or set
# BUSYBOX to one) and 'make stage' will use it as-is.
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*)
        echo "busybox: configuring busybox needs a POSIX host: build it on" >&2
        echo "busybox:       Linux, WSL or in a container, then copy the" >&2
        echo "busybox:       static aarch64 binary to out/bin/busybox, or set" >&2
        echo "busybox:       BUSYBOX to it - 'make stage' uses it unchanged" >&2
        exit 1
        ;;
esac

mkdir -p "$SRC" "$OUT/bin"

if [ ! -f "$TARBALL" ]; then
    command -v curl >/dev/null 2>&1 || die "curl is required to fetch busybox"
    echo "busybox: fetching $VER"
    curl -fL "https://busybox.net/downloads/busybox-$VER.tar.bz2" \
         -o "$TARBALL" || die "download failed; set BUSYBOX_SRC to a local tree"
fi

BUILD="$OUT/busybox-build"
rm -rf "$BUILD"
mkdir -p "$BUILD"
tar -xf "$TARBALL" -C "$BUILD" --strip-components=1

cd "$BUILD"
make CROSS_COMPILE="$CROSS_COMPILE" allnoconfig >/dev/null

# Turn the applet list into CONFIG_ symbols, then flip them on. Anything the
# list asks for that does not exist in this version is reported instead of
# silently missing from the image.
tmp="$OUT/busybox-config.sed"
missing=""
: > "$tmp"
for a in $APPLETS; do
    if grep -q "CONFIG_$a" .config 2>/dev/null; then
        printf 's/^# CONFIG_%s is not set/CONFIG_%s=y/\n' "$a" "$a" >> "$tmp"
    else
        missing="$missing $a"
    fi
done
sed -i -f "$tmp" .config
rm -f "$tmp"

if [ -n "$missing" ]; then
    echo "busybox: WARNING: these names have no CONFIG symbol in $VER:" >&2
    echo "         $missing" >&2
    echo "         rename them in APPLETS above; the image will lack them." >&2
fi

make CROSS_COMPILE="$CROSS_COMPILE" OLDCONFIG=n CONFIG_STATIC=y \
     -j"$(nproc 2>/dev/null || echo 4)" >/dev/null

[ -x "_install/binaries/busybox" ] || [ -x "busybox" ] || \
    die "busybox produced no binary; run the build by hand to see the errors"

if [ -f "busybox" ]; then
    cp -f busybox "$OUT/bin/busybox"
else
    cp -f _install/binaries/busybox "$OUT/bin/busybox"
fi

echo "busybox: $(ls -l "$OUT/bin/busybox" | awk '{print $5}') bytes static"
echo "busybox: now run 'make stage' to rebuild the initramfs"
