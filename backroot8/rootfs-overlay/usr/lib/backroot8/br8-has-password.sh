#!/bin/sh
# Exit 0 if the desktop session user has a password set.
set -eu

USER_NAME="${USER:-root}"
if [ -r /etc/backroot8/desktop-user ]; then
    _u="$(tr -d '[:space:]' </etc/backroot8/desktop-user)"
    [ -n "$_u" ] && USER_NAME="$_u"
fi

STATUS="$(passwd -S "$USER_NAME" 2>/dev/null || true)"
case "$STATUS" in
    *" P "*) exit 0 ;;
    *" L "*) exit 0 ;;
    *) exit 1 ;;
esac
