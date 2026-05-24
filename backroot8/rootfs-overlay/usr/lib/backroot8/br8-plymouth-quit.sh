#!/bin/sh
# End Plymouth once the X session has had a moment to map the panel.

command -v plymouth >/dev/null 2>&1 || exit 0
plymouth --ping 2>/dev/null || exit 0

if [ -f /run/br8-oobe/keep-loading ]; then
    for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25; do
        [ -f /run/br8-oobe/panel-ready ] && break
        sleep 0.2
    done
else
    sleep 0.8
fi

exec plymouth quit
