#!/bin/sh
# Show boot splash on tty1 / framebuffer until the desktop service stops us.
IMG=/usr/share/backroot8/bootscreen.png
[ -r "$IMG" ] || exit 0

if command -v setterm >/dev/null; then
    setterm -background black -foreground white -term linux -reset 2>/dev/null || true
fi
clear 2>/dev/null || true

if ! command -v fbi >/dev/null; then
    exit 0
fi

# -fit: letterbox entire splash on screen (do not crop/zoom into the logo only)
if [ -c /dev/fb0 ]; then
    exec fbi -T 1 -d /dev/fb0 -noverbose -fit "$IMG" </dev/tty1
fi
exec fbi -T 1 -noverbose -fit "$IMG" </dev/tty1
