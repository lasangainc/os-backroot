#!/usr/bin/env bash
# Run Backroot 8 in QEMU with SDL display (local GUI)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DISK="$ROOT/vm/backroot8.img"
RAM_MB="${RAM_MB:-2048}"

exec qemu-system-x86_64 \
    -name backroot8 \
    -machine pc \
    -cpu qemu64 \
    -m "$RAM_MB" \
    -smp 2 \
    -drive file="$DISK",format=raw,if=ide \
    -netdev user,id=net0 \
    -device virtio-net-pci,netdev=net0 \
    -vga std \
    -display sdl \
    -usb -device usb-tablet
