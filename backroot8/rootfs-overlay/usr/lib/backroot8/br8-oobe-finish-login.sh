#!/bin/bash
# Switch to the new user desktop after OOBE (runs from the root X session).
set -euo pipefail

[[ -f /etc/backroot8/desktop-user ]] || exit 0
[[ -f /etc/backroot8/oobe-complete ]] || exit 0

systemctl restart backroot8-desktop.service
