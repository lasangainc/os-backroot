#!/usr/bin/env bash
# Start Backroot 8 VM and expose the desktop in your browser (noVNC).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WEB_PORT="${WEB_PORT:-6080}"
VNC_PORT="${VNC_PORT:-5902}"
WEBSOCK_PID="$ROOT/vm/websockify.pid"

"$ROOT/scripts/run-vm.sh"

if ! command -v websockify >/dev/null; then
    echo "Installing noVNC + websockify..."
    sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq novnc websockify
fi

if [[ -f "$WEBSOCK_PID" ]]; then
    OLD="$(cat "$WEBSOCK_PID")"
    if ps -p "$OLD" -o stat= 2>/dev/null | grep -qv '^Z'; then
        echo "noVNC proxy already running (PID $OLD)"
        echo ""
        echo "  Open in your browser:  http://localhost:${WEB_PORT}/vnc.html?autoconnect=1"
        echo "  (In Cursor: Ports panel → forward ${WEB_PORT} if needed)"
        exit 0
    fi
    rm -f "$WEBSOCK_PID"
fi

pkill -f "websockify.*127.0.0.1:${VNC_PORT}" 2>/dev/null || true
sleep 1

nohup websockify --web /usr/share/novnc \
    "0.0.0.0:${WEB_PORT}" "127.0.0.1:${VNC_PORT}" \
    > "$ROOT/vm/websockify.log" 2>&1 &
echo $! > "$WEBSOCK_PID"

sleep 1
echo ""
echo "══════════════════════════════════════════════════════════"
echo "  Backroot 8 GUI is ready"
echo "══════════════════════════════════════════════════════════"
echo ""
echo "  Browser (recommended):"
echo "    http://localhost:${WEB_PORT}/vnc.html?autoconnect=1&resize=scale"
echo ""
echo "  VNC client (optional):"
echo "    localhost:${VNC_PORT}"
echo ""
echo "  Cursor: open the Ports tab and forward port ${WEB_PORT},"
echo "          then click the forwarded URL."
echo ""
echo "  VM PID: $(cat "$ROOT/vm/qemu.pid" 2>/dev/null || echo unknown)"
echo "══════════════════════════════════════════════════════════"
