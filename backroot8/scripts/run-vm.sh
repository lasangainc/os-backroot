#!/usr/bin/env bash
# Run Backroot 8 in QEMU from the live ISO (headless + VNC).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ISO="${ISO:-$ROOT/vm/backroot8-live.iso}"
PIDFILE="$ROOT/vm/qemu.pid"
VNC_DISPLAY="${VNC_DISPLAY:-:2}"
VNC_PORT="${VNC_PORT:-5902}"
RAM_MB="${RAM_MB:-2048}"
CPUS="${CPUS:-2}"

if [[ ! -f "$ISO" ]]; then
    echo "ISO not found: $ISO" >&2
    echo "Build with: sudo ./scripts/build-iso.sh" >&2
    exit 1
fi

if [[ -f "$PIDFILE" ]]; then
    OLD_PID="$(cat "$PIDFILE")"
    STAT="$(ps -p "$OLD_PID" -o stat= 2>/dev/null || true)"
    COMM="$(ps -p "$OLD_PID" -o comm= 2>/dev/null || true)"
    if [[ -n "$STAT" && "$STAT" != Z* && "$COMM" == *qemu-system* ]]; then
        echo "QEMU already running (PID $OLD_PID)"
        echo "VNC: localhost:${VNC_PORT} (display ${VNC_DISPLAY})"
        exit 0
    fi
    rm -f "$PIDFILE"
fi
pkill -f "qemu-system-x86_64.*-name backroot8" 2>/dev/null || true
sleep 1

mkdir -p "$ROOT/vm"

KVM_ARGS=()
if [[ -r /dev/kvm ]] && { [[ "$(id -u)" -eq 0 ]] || groups | grep -q kvm; }; then
    KVM_ARGS=(-cpu host -enable-kvm)
else
    KVM_ARGS=(-cpu qemu64)
fi

nohup qemu-system-x86_64 \
    -name backroot8 \
    -machine pc \
    "${KVM_ARGS[@]}" \
    -m "$RAM_MB" \
    -smp "$CPUS" \
    -cdrom "$ISO" \
    -boot d \
    -netdev user,id=net0,hostfwd=tcp::2222-:22 \
    -device virtio-net-pci,netdev=net0 \
    -vga std \
    -display none \
    -vnc "0.0.0.0${VNC_DISPLAY}" \
    -usb -device usb-tablet \
    -rtc base=utc \
    -serial file:"$ROOT/vm/serial.log" \
    > "$ROOT/vm/qemu.log" 2>&1 &

echo $! > "$PIDFILE"
echo "Backroot 8 VM started from ISO (PID $(cat "$PIDFILE"))"
echo "GUI:  run ./scripts/run-vm-gui.sh  OR  http://localhost:6080/vnc.html?autoconnect=1"
echo "VNC:  localhost:${VNC_PORT} (display ${VNC_DISPLAY})"
