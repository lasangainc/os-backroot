#!/bin/sh
# Run br8-panel; on crash write /tmp/br8-panel.crash and wait for WM restart signal.
PANEL=/usr/local/bin/br8-panel
ERR=/tmp/br8-panel.stderr
CRASH=/tmp/br8-panel.crash
RESTART=/tmp/br8-panel.restart

rm -f "$CRASH" "$RESTART" "$ERR"

while true; do
    : >"$ERR"
    if "$PANEL" 2>>"$ERR"; then
        rm -f "$CRASH" "$ERR"
    else
        status=$?
        {
            printf 'br8-panel exited with status %s\n\n' "$status"
            cat "$ERR" 2>/dev/null
        } >"$CRASH"
        while [ -f "$CRASH" ] && [ ! -f "$RESTART" ]; do
            sleep 0.25
        done
        rm -f "$CRASH" "$RESTART" "$ERR"
    fi
    sleep 0.5
done
