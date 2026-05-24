#!/bin/bash
# Switch to the new user desktop after OOBE (runs from the root X session).
set -euo pipefail

[[ -f /etc/backroot8/desktop-user ]] || exit 0
[[ -f /etc/backroot8/oobe-complete ]] || exit 0

mkdir -p /run/br8-oobe
chmod 1777 /run/br8-oobe
touch /run/br8-oobe/keep-loading

# Stop/start outside this service cgroup (restart from inside the unit is unreliable).
systemd-run --no-block --collect \
    /bin/bash -c 'sleep 0.5; systemctl stop backroot8-desktop.service; sleep 1; systemctl start backroot8-desktop.service'
