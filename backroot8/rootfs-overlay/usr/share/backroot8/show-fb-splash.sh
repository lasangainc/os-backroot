#!/bin/sh
# Framebuffer boot emblem (QEMU/live): used when Plymouth has no visible surface.
IMG=/usr/share/backroot8/bootscreen.png
[ -r "$IMG" ] || exit 0
grep -q 'br8\.debug' /proc/cmdline && exit 0
grep -q 'plymouth\.enable=0' /proc/cmdline && exit 0
command -v fbi >/dev/null || exit 0
[ -c /dev/fb0 ] || exit 0

if command -v setterm >/dev/null; then
    setterm -background black -foreground white -term linux -reset 2>/dev/null || true
fi
clear 2>/dev/null || true

exec fbi -T 1 -d /dev/fb0 -noverbose -fit "$IMG" </dev/null
