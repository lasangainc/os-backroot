#!/bin/bash
# Start the X desktop as the configured primary user (root before OOBE).
set -euo pipefail

DESKTOP_USER=root
if [[ -f /etc/backroot8/desktop-user ]]; then
    DESKTOP_USER="$(tr -d '[:space:]' </etc/backroot8/desktop-user)"
fi
[[ -n "$DESKTOP_USER" ]] || DESKTOP_USER=root

if ! id "$DESKTOP_USER" &>/dev/null; then
    DESKTOP_USER=root
fi

export HOME
HOME="$(getent passwd "$DESKTOP_USER" | cut -d: -f6)"
[[ -n "$HOME" ]] || HOME="/home/$DESKTOP_USER"

UID_NUM="$(id -u "$DESKTOP_USER")"
export XDG_RUNTIME_DIR="/run/user/$UID_NUM"
mkdir -p "$XDG_RUNTIME_DIR"
chown "$DESKTOP_USER:$DESKTOP_USER" "$XDG_RUNTIME_DIR" 2>/dev/null || true
chmod 700 "$XDG_RUNTIME_DIR" 2>/dev/null || true

exec runuser -u "$DESKTOP_USER" -- /usr/bin/startx /etc/X11/xinit/xinitrc -- /usr/bin/X vt1 -keeptty
