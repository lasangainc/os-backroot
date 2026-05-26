#!/bin/sh
# End the X session and return to lock screen on next desktop start.
set -eu
if [ "$(id -u)" -eq 0 ]; then
    systemctl restart backroot8-desktop.service
else
    sudo /usr/bin/systemctl restart backroot8-desktop.service
fi
