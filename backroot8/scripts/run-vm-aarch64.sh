#!/usr/bin/env bash
# Run Backroot 8 aarch64 image in QEMU (virt machine, serial console + optional VNC)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DISK="${DISK:-$ROOT/vm/backroot8-aarch64.img}"
MNT="$ROOT/vm/mnt-aarch64"
PIDFILE="$ROOT/vm/qemu-aarch64.pid"
VNC_DISPLAY="${VNC_DISPLAY:-:3}"
VNC_PORT="${VNC_PORT:-5903}"
RAM_MB="${RAM_MB:-2048}"
CPUS="${CPUS:-2}"
KERNEL="$ROOT/vm/vmlinuz-linux-aarch64"
INITRD="$ROOT/vm/initramfs-linux-aarch64.img"
FW_DIR="${FW_DIR:-/usr/share/qemu-efi-aarch64}"
FW="${FW:-$FW_DIR/QEMU_EFI.fd}"

if [[ ! -f "$DISK" ]]; then
    echo "Disk image not found. Run: BACKROOT8_ARCH=aarch64 sudo ./scripts/build-rootfs.sh" >&2
    echo "Or: sudo ./scripts/build-milestone1.sh" >&2
    exit 1
fi

if [[ ! -f "$KERNEL" ]] || [[ ! -f "$INITRD" ]]; then
    mkdir -p "$MNT"
    if ! mountpoint -q "$MNT"; then
        sudo mount -o loop,ro "$DISK" "$MNT"
    fi
    sudo cp "$MNT/boot/vmlinuz-linux" "$KERNEL"
    sudo cp "$MNT/boot/initramfs-linux.img" "$INITRD"
    sudo chmod a+r "$KERNEL" "$INITRD"
    sudo chown "$(id -un):$(id -gn)" "$KERNEL" "$INITRD" 2>/dev/null || true
    mountpoint -q "$MNT" && sudo umount "$MNT"
fi

[[ -f "$DISK" ]] && sudo chown "$(id -un):$(id -gn)" "$DISK" 2>/dev/null || true

if [[ -f "$PIDFILE" ]]; then
    OLD_PID="$(cat "$PIDFILE")"
    STAT="$(ps -p "$OLD_PID" -o stat= 2>/dev/null || true)"
    COMM="$(ps -p "$OLD_PID" -o comm= 2>/dev/null || true)"
    if [[ -n "$STAT" && "$STAT" != Z* && "$COMM" == *qemu-system* ]]; then
        echo "QEMU aarch64 already running (PID $OLD_PID)"
        echo "VNC: localhost:${VNC_PORT} (display ${VNC_DISPLAY})"
        exit 0
    fi
    rm -f "$PIDFILE"
fi
pkill -f "qemu-system-aarch64.*-name backroot8-aarch64" 2>/dev/null || true
sleep 1

if ! command -v qemu-system-aarch64 >/dev/null 2>&1; then
    echo "Install qemu-system-arm / qemu-system-aarch64" >&2
    exit 1
fi

MACHINE_ARGS=(-machine virt -cpu cortex-a72)
if [[ -f "$FW" ]]; then
    MACHINE_ARGS+=(-bios "$FW")
fi

nohup qemu-system-aarch64 \
    -name backroot8-aarch64 \
    "${MACHINE_ARGS[@]}" \
    -m "$RAM_MB" \
    -smp "$CPUS" \
    -kernel "$KERNEL" \
    -initrd "$INITRD" \
    -append "root=LABEL=backroot8-aarch64 rw console=ttyAMA0" \
    -drive file="$DISK",format=raw,if=virtio \
    -netdev user,id=net0,hostfwd=tcp::2223-:22 \
    -device virtio-net-pci,netdev=net0 \
    -device virtio-gpu-pci \
    -display none \
    -vnc "0.0.0.0${VNC_DISPLAY}" \
    -rtc base=utc \
    -serial file:"$ROOT/vm/serial-aarch64.log" \
    > "$ROOT/vm/qemu-aarch64.log" 2>&1 &

echo $! > "$PIDFILE"
echo "Backroot 8 aarch64 VM started (PID $(cat "$PIDFILE"))"
echo "VNC: localhost:${VNC_PORT} (display ${VNC_DISPLAY})"
echo "SSH: ssh -p 2223 root@127.0.0.1  (password: backroot8)"
