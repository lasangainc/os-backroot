#!/usr/bin/env bash
# Run Backroot 8 in QEMU from the live ISO with SDL display (local GUI).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ISO="${ISO:-$ROOT/vm/backroot8-live.iso}"
RAM_MB="${RAM_MB:-2048}"

if [[ ! -f "$ISO" ]]; then
    echo "ISO not found: $ISO (run: sudo ./scripts/build-iso.sh)" >&2
    exit 1
fi

exec qemu-system-x86_64 \
    -name backroot8 \
    -machine pc \
    -cpu qemu64 \
    -m "$RAM_MB" \
    -smp 2 \
    -cdrom "$ISO" \
    -boot d \
    -netdev user,id=net0 \
    -device virtio-net-pci,netdev=net0 \
    -vga std \
    -display sdl \
    -usb -device usb-tablet
