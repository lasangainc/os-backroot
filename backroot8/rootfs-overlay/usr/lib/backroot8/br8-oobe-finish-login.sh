#!/bin/bash
# Legacy helper — OOBE setup now restarts the desktop itself.
set -euo pipefail

[[ -f /etc/backroot8/desktop-user ]] || exit 0
[[ -f /etc/backroot8/oobe-complete ]] || exit 0

mkdir -p /run/br8-oobe
chmod 1777 /run/br8-oobe
touch /run/br8-oobe/keep-loading

systemd-run --no-block --collect \
    /usr/bin/systemctl restart backroot8-desktop.service
