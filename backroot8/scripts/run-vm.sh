#!/usr/bin/env bash
# Run Backroot 8 in QEMU (headless with VNC for remote access)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DISK="$ROOT/vm/backroot8.img"
MNT="$ROOT/vm/mnt"
PIDFILE="$ROOT/vm/qemu.pid"
VNC_DISPLAY="${VNC_DISPLAY:-:2}"
VNC_PORT="${VNC_PORT:-5902}"
RAM_MB="${RAM_MB:-2048}"
CPUS="${CPUS:-2}"
KERNEL="$ROOT/vm/vmlinuz-linux"
INITRD="$ROOT/vm/initramfs-linux.img"

if [[ ! -f "$DISK" ]]; then
    echo "Disk image not found. Run scripts/build-rootfs.sh first." >&2
    exit 1
fi

# Extract kernel/initrd for direct QEMU boot (whole-disk ext4 has no GRUB embed room)
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
    if [[ -n "$STAT" && "$STAT" != Z* ]]; then
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
if [[ -r /dev/kvm ]] && [[ "$(id -u)" -eq 0 ]] || groups | grep -q kvm; then
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
    -kernel "$KERNEL" \
    -initrd "$INITRD" \
    -append "root=LABEL=backroot8 rw console=ttyS0" \
    -drive file="$DISK",format=raw,if=ide \
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
echo "Backroot 8 VM started (PID $(cat "$PIDFILE"))"
echo "VNC: connect to localhost:${VNC_PORT} (display ${VNC_DISPLAY})"
echo "SSH: ssh -p 2222 root@localhost (password: backroot8)"
