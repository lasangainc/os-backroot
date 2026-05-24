#!/bin/sh
# Framebuffer boot emblem (QEMU/live): used when Plymouth has no visible surface.
set -eu

# shellcheck source=/dev/null
. /usr/lib/backroot8/br8-bootscreen.sh

grep -q 'br8\.debug' /proc/cmdline && exit 0
grep -q 'plymouth\.enable=0' /proc/cmdline && exit 0
command -v fbi >/dev/null || exit 0
[ -c /dev/fb0 ] || exit 0

IMG=$(br8_pick_bootscreen) || exit 0

if command -v setterm >/dev/null; then
    setterm -background black -foreground white -term linux -reset 2>/dev/null || true
fi
clear 2>/dev/null || true

# Validate before fbi so a bad PNG never paints "loading FAILED" on the console.
exec fbi -T 1 -d /dev/fb0 -noverbose -fit "$IMG" </dev/null 2>/dev/null
