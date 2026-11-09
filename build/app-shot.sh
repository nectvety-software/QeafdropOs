# build/app-shot.sh - a program from a .qeapp package, running for real, and the
# pictures its glass makes.
#
# Everything before this script was a page the OS paints. This one is the other
# half of what the user asked for: press the row of an installed app, and the app
# starts. The simulator cannot do that by itself - its fork() is a table entry
# (sim/sim_board.c says so) and the payload inside a package is an ARM ELF, which
# this machine cannot execute - so the run puts the two halves beside each other:
#
#   init/status.c decides, unpacks and forks, through the shipped code path, and
#   sim/qeafsim.c's watch starts build/app-shot.sh's own PC build of the very same
#   apps/demo/demo.c the moment init says a package holds the glass.
#
# The program then asks for the panel through the file door, and its request goes
# through ctl_command()'s `frame` verb, media_rgb_read() and panel_frame_screen() -
# all three of them the halves a phone runs. So the pixels in the two app pictures
# are the shipped painter's, and the program that chose them is a real process with
# a real pid on this machine, started after the press and ended by the press that
# gives the glass back.
#
# The gate (build/check-app-shot.py) reads the transcript, the run directory and
# the PNGs, and one of its rows is the whole reason for the file: the two app
# pictures have to *differ*, in the band's row and in the lit cell, because a
# program that started and then stopped paints the same picture twice.
#
#   sh build/app-shot.sh
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"

RUN=out/app-run
MEDIA=out/media
WORK=out/sim-work-app
TARGET=${TARGET:-x86_64-windows-gnu}
case $(uname -s) in
    Linux*) TARGET=x86_64-linux-gnu ;;
esac

# The package tree first: the program's payload has to exist before the packer will
# write an app package, and the drawer in the pictures is the drawer the image
# ships rather than one this harness assembled by hand.
sh build/mk-app-demo.sh
python build/mk-media.py "$MEDIA"
python build/mk-qeapp.py "$MEDIA"
sh build/sim-build.sh

# The same source, this time for the machine the pictures are taken on. No second
# implementation of the app: the header's file branch is the only thing that
# differs from the payload, and -Werror is the same wall the phone's build hits.
if [ -z "${HOSTCC:-}" ]; then
    ZIG=$(ls "$ROOT"/out/toolchain/zig-*/zig.exe "$ROOT"/out/toolchain/zig-*/zig \
            2>/dev/null | head -1 || true)
    [ -n "$ZIG" ] || {
        echo "app-shot: no zig under out/toolchain and no HOSTCC - run" >&2
        echo "app-shot:   build/get-toolchain.sh, or set HOSTCC=cc" >&2
        exit 1
    }
    HOSTCC="$ZIG cc -target $TARGET"
fi
# shellcheck disable=SC2086  # the compiler is a word-split command on purpose
$HOSTCC -std=gnu99 -Wall -Wextra -Wshadow -Werror -D_GNU_SOURCE \
    -DQEAF_APP_FILE_IPC -o "$RUN-host-tmp.exe" apps/demo/demo.c
rm -rf "$RUN"
mkdir -p "$RUN"
mv "$RUN-host-tmp.exe" "$RUN/demo-host.exe"

# Every path below is relative to this directory and both processes run with it as
# their cwd: the door, the media tree, the work tree and the pictures. A native
# Windows process has no use for a shell's /d/... spelling, and the simulator's
# path mapper passes anything that does not start with '/' straight through - so
# the host spelling of /tmp/qeaf-apps/<id>, which is what the watch hands the
# program as $QEAF_APP_DIR, resolves the same way in both.
rm -rf "$WORK"
rm -f "$RUN"/ctl.req "$RUN"/ctl.ans
# A door that still holds a request from a run that was killed would be answered
# twice; the work tree is wiped by the simulator anyway, so the run directory
# inside it cannot carry an old app.bin into a new picture.

# shellcheck disable=SC2086  # the two variables are the door and the program
RC=0
env QEAF_APP_IPC="$RUN" QEAF_APP_HOST="$RUN/demo-host.exe" \
    out/qeafsim.exe --headless --media "$MEDIA" --work "$WORK" \
    --script tests/app-run.sim > "$RUN/transcript.txt" 2>&1 || RC=$?
echo "app-shot: simulator rc $RC"
sed -n '1,999p' "$RUN/transcript.txt" | grep -E '^(sim:|== |< app:|> screen)' || true

[ "$RC" = 0 ] || {
    echo "app-shot: the simulator did not finish the walk" >&2
    exit 1
}
python build/check-app-shot.py "$RUN/transcript.txt" \
    "$RUN/01-drawer.png" "$RUN/02-drawer-demo.png" \
    "$RUN/03-app-first.png" "$RUN/04-app-later.png" \
    "$RUN/05-launcher-again.png" "$WORK/tmp/qeaf-apps/demo"

echo "app-shot: open 03-app-first.png and 04-app-later.png - two frames of one"
echo "app-shot: program, not one frame photographed twice"
