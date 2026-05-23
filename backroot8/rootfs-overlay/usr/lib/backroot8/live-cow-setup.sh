#!/bin/bash
# Live ISO: ensure writable state for systemd and X (after initramfs root is up).
set -euo pipefail

grep -q 'backroot8iso' /proc/cmdline || exit 0

mount -o remount,rw / 2>/dev/null || true

for d in /var/lib/systemd /var/log/journal /var/tmp /tmp; do
    mkdir -p "$d"
done

chmod 755 /var/lib/systemd 2>/dev/null || true
