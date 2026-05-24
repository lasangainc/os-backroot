#!/bin/sh
# Framebuffer boot emblem when Plymouth cannot paint the theme (QEMU -vga std).
# Must start before plymouth-start: Plymouth grabs tty1 and often makes /dev/fb0 unusable.
set -eu

# shellcheck source=/dev/null
. /usr/lib/backroot8/br8-bootscreen.sh

grep -q 'br8\.debug' /proc/cmdline && exit 0
grep -q 'plymouth\.enable=0' /proc/cmdline && exit 0
command -v fbi >/dev/null || exit 0
[ -c /dev/fb0 ] || exit 0
plymouth --ping >/dev/null 2>&1 && exit 0

IMG=$(br8_pick_bootscreen) || exit 0

if command -v setterm >/dev/null; then
    setterm -background black -foreground white -term linux -reset 2>/dev/null || true
fi
clear 2>/dev/null || true

# No -d /dev/fb0: wrong device after KMS/Plymouth; let fbi pick the active VT framebuffer.
exec fbi --noverbose -a "$IMG" </dev/null 2>/dev/null
