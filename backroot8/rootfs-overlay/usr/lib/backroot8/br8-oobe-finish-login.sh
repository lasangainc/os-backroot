#!/bin/bash
# Legacy helper — OOBE setup now reboots the system itself.
set -euo pipefail

[[ -f /etc/backroot8/desktop-user ]] || exit 0
[[ -f /etc/backroot8/oobe-complete ]] || exit 0

systemd-run --no-block --collect /usr/bin/systemctl reboot
