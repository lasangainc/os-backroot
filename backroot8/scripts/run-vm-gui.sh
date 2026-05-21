#!/usr/bin/env bash
# Start Backroot 8 VM and expose the desktop in your browser (noVNC).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WEB_PORT="${WEB_PORT:-6080}"
VNC_PORT="${VNC_PORT:-5902}"
WEBSOCK_PID="$ROOT/vm/websockify.pid"
FORCE="${FORCE:-0}"

stop_websockify() {
    if [[ -f "$WEBSOCK_PID" ]]; then
        OLD="$(cat "$WEBSOCK_PID")"
        kill "$OLD" 2>/dev/null || true
        rm -f "$WEBSOCK_PID"
    fi
    pkill -f "websockify.*:${WEB_PORT}" 2>/dev/null || true
    pkill -f "websockify.*127.0.0.1:${VNC_PORT}" 2>/dev/null || true
    sleep 1
}

start_websockify() {
    if ! command -v websockify >/dev/null; then
        echo "Installing noVNC + websockify..."
        sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq novnc websockify
    fi

    stop_websockify

    nohup websockify --web /usr/share/novnc \
        --heartbeat 30 \
        "0.0.0.0:${WEB_PORT}" "127.0.0.1:${VNC_PORT}" \
        > "$ROOT/vm/websockify.log" 2>&1 &
    echo $! > "$WEBSOCK_PID"
    sleep 1
}

wait_vnc() {
    local i
    for i in $(seq 1 30); do
        if python3 -c "import socket; s=socket.create_connection(('127.0.0.1',${VNC_PORT}),2); s.close()" 2>/dev/null; then
            return 0
        fi
        sleep 1
    done
    echo "Warning: VNC port ${VNC_PORT} not accepting connections yet (VM may still be booting)." >&2
    return 1
}

# Restart QEMU unless already healthy
QEMU_OK=0
if [[ -f "$ROOT/vm/qemu.pid" ]]; then
    QP="$(cat "$ROOT/vm/qemu.pid")"
    if ps -p "$QP" -o stat= 2>/dev/null | grep -qv '^Z' \
        && ps -p "$QP" -o comm= 2>/dev/null | grep -q qemu-system; then
        QEMU_OK=1
    fi
fi

if [[ "$FORCE" == "1" || "$QEMU_OK" == "0" ]]; then
    pkill -f "qemu-system-x86_64.*-name backroot8" 2>/dev/null || true
    sleep 1
    rm -f "$ROOT/vm/qemu.pid"
    "$ROOT/scripts/run-vm.sh"
else
    echo "QEMU already running (PID $(cat "$ROOT/vm/qemu.pid"))"
fi

start_websockify
wait_vnc || true

# URLs: hash params work over HTTPS port forwards (encrypt=1 for wss)
HTTP_URL="http://127.0.0.1:${WEB_PORT}/vnc.html?autoconnect=1&resize=scale&path=websockify"
HTTPS_URL="http://127.0.0.1:${WEB_PORT}/vnc.html#autoconnect=1&resize=scale&path=websockify&encrypt=1"

cat > "$ROOT/vm/novnc.url" <<EOF
${HTTP_URL}
EOF

echo ""
echo "══════════════════════════════════════════════════════════"
echo "  Backroot 8 GUI is ready"
echo "══════════════════════════════════════════════════════════"
echo ""
echo "  IMPORTANT (Cursor / remote): do NOT open localhost on your"
echo "  own PC. Use the forwarded port from the Ports tab."
echo ""
echo "  1. Open Cursor → Ports → forward port ${WEB_PORT}"
echo "  2. Click the forwarded URL (https://... is normal)"
echo "  3. If you see 'failed to connect', use this URL instead:"
echo "     ${HTTPS_URL}"
echo ""
echo "  On the agent machine (local only):"
echo "     ${HTTP_URL}"
echo ""
echo "  VNC: 127.0.0.1:${VNC_PORT}"
echo "  VM PID: $(cat "$ROOT/vm/qemu.pid" 2>/dev/null || echo unknown)"
echo "  websockify PID: $(cat "$WEBSOCK_PID" 2>/dev/null || echo unknown)"
echo "══════════════════════════════════════════════════════════"
