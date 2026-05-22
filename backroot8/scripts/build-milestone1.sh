#!/usr/bin/env bash
# Milestone 1: compile desktop components and build x86_64 + aarch64 disk images.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VM_DIR="$ROOT/vm"
LOG="$VM_DIR/build-milestone1.log"

log() { echo "[milestone1] $*" | tee -a "$LOG"; }

mkdir -p "$VM_DIR"
: >"$LOG"

log "Backroot 8 Milestone 1 — x86_64 disk image + bootable ISO"
log "Host: $(uname -m) — $(date -u +%Y-%m-%dT%H:%M:%SZ)"

if ! command -v mksquashfs >/dev/null 2>&1; then
    log "Installing ISO build tools..."
    sudo apt-get update -qq
    sudo apt-get install -y -qq squashfs-tools xorriso grub-pc-bin grub-efi-amd64-bin mtools
fi

log "=== x86_64 rootfs ==="
BACKROOT8_ARCH=x86_64 sudo -E "$ROOT/scripts/build-rootfs.sh" 2>&1 | tee -a "$LOG"

log "=== x86_64 bootable ISO ==="
sudo -E "$ROOT/scripts/build-iso.sh" 2>&1 | tee -a "$LOG"

ln -sfn backroot8-x86_64.img "$VM_DIR/backroot8.img"

log "Milestone 1 images:"
ls -lh "$VM_DIR"/backroot8-x86_64.img "$VM_DIR"/backroot8-aarch64.img "$VM_DIR"/backroot8.img 2>&1 | tee -a "$LOG"
log "Done. ISO: $VM_DIR/backroot8-x86_64.iso  |  disk VM: ./scripts/run-vm-gui.sh"
