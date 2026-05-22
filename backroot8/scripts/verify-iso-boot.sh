#!/usr/bin/env bash
# Boot the Milestone 1 ISO in QEMU and fail on kernel panic / boot errors.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ISO="${ISO:-$ROOT/vm/backroot8-milestone1.iso}"
SERIAL_LOG="$ROOT/vm/iso-boot-serial.log"
TIMEOUT_SEC="${BOOT_VERIFY_TIMEOUT:-900}"
RAM_MB="${RAM_MB:-2048}"

if [[ ! -f "$ISO" ]]; then
    echo "ISO not found: $ISO (run scripts/build-iso.sh)" >&2
    exit 1
fi

pkill -f 'qemu-system-x86_64.*-name backroot8-iso' 2>/dev/null || true
sleep 1
rm -f "$SERIAL_LOG"

KVM_ARGS=(-cpu qemu64)
if [[ -r /dev/kvm ]]; then
    KVM_ARGS=(-cpu host -enable-kvm)
fi

log() { echo "[verify-iso] $*"; }
log "Booting $ISO (timeout ${TIMEOUT_SEC}s)..."

qemu-system-x86_64 \
    -name backroot8-iso \
    -machine pc \
    "${KVM_ARGS[@]}" \
    -m "$RAM_MB" \
    -cdrom "$ISO" \
    -boot d \
    -vga std \
    -display none \
    -serial file:"$SERIAL_LOG" \
    -netdev user,id=net0 \
    -device virtio-net-pci,netdev=net0 \
    -rtc base=utc \
    > "$ROOT/vm/iso-boot-qemu.log" 2>&1 &
QEMU_PID=$!

fail() {
    log "FAIL: $*"
    kill "$QEMU_PID" 2>/dev/null || true
    wait "$QEMU_PID" 2>/dev/null || true
    if [[ -f "$SERIAL_LOG" ]]; then
        log "Last 40 lines of serial log:"
        tail -40 "$SERIAL_LOG" >&2
    fi
    exit 1
}

deadline=$((SECONDS + TIMEOUT_SEC))
ok=0
while (( SECONDS < deadline )); do
    if ! kill -0 "$QEMU_PID" 2>/dev/null; then
        fail "QEMU exited early"
    fi
    if [[ -f "$SERIAL_LOG" ]]; then
        if grep -qE 'Kernel panic|VFS: Cannot open root device|Failed to mount|ALERT!.*failed' "$SERIAL_LOG"; then
            fail "boot error detected in serial log"
        fi
        if grep -q 'Backroot 8 Milestone 1' "$SERIAL_LOG" 2>/dev/null || \
           grep -qE 'Started backroot8-desktop|Reached target.*Graphical|login:' "$SERIAL_LOG"; then
            ok=1
            break
        fi
        if grep -q 'Welcome to Backroot' "$SERIAL_LOG" 2>/dev/null && \
           grep -q 'systemd' "$SERIAL_LOG" && \
           grep -qE 'backroot8-desktop|startx|Xorg' "$SERIAL_LOG"; then
            ok=1
            break
        fi
    fi
    sleep 5
done

kill "$QEMU_PID" 2>/dev/null || true
wait "$QEMU_PID" 2>/dev/null || true

if [[ "$ok" -ne 1 ]]; then
    fail "timed out waiting for successful boot markers"
fi

log "PASS: ISO booted without detected errors"
log "Serial log: $SERIAL_LOG"
