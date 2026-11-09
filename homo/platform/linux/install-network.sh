#!/bin/sh
set -eu

# Run as root. Only this fixed adapter is granted passwordless execution.
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
command -v python3 >/dev/null
install -d -o root -g root -m 755 /usr/local/libexec
install -o root -g root -m 755 "$HERE/hachimi-network" /usr/local/libexec/hachimi-network
RULE=$(mktemp)
trap 'rm -f "$RULE"' EXIT
printf '%s\n' 'debian ALL=(root) NOPASSWD: /usr/local/libexec/hachimi-network ""' > "$RULE"
visudo -cf "$RULE"
install -o root -g root -m 440 "$RULE" /etc/sudoers.d/hachimi-network

# Bluetooth is optional at image-build time. If BlueZ is present on the target,
# start its daemon and clear a software rfkill block; never pretend an absent
# controller or missing firmware is available.
if command -v bluetoothctl >/dev/null 2>&1; then
    if command -v systemctl >/dev/null 2>&1 && systemctl cat bluetooth.service >/dev/null 2>&1; then
        systemctl enable bluetooth.service >/dev/null 2>&1 || true
        systemctl start bluetooth.service >/dev/null 2>&1 || true
    fi
    if command -v rfkill >/dev/null 2>&1; then
        rfkill unblock bluetooth >/dev/null 2>&1 || true
    fi
fi
