#!/usr/bin/env bash
# Milestone 1: compile desktop components and build x86_64 + aarch64 disk images.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VM_DIR="$ROOT/vm"
LOG="$VM_DIR/build-milestone1.log"

log() { echo "[milestone1] $*" | tee -a "$LOG"; }

mkdir -p "$VM_DIR"
: >"$LOG"

log "Backroot 8 Milestone 1 — dual-arch disk image build (x86_64: Arch, aarch64: Arch Linux ARM)"
log "Host: $(uname -m) — $(date -u +%Y-%m-%dT%H:%M:%SZ)"

if [[ "$(uname -m)" == "x86_64" ]]; then
    if ! command -v qemu-aarch64-static >/dev/null 2>&1; then
        log "Installing qemu-user-static for aarch64 chroot..."
        sudo apt-get update -qq
        sudo apt-get install -y -qq qemu-user-static qemu-system-arm binfmt-support
    fi
    if [[ ! -f /proc/sys/fs/binfmt_misc/qemu-aarch64 ]]; then
        sudo mount -t binfmt_misc binfmt_misc /proc/sys/fs/binfmt_misc 2>/dev/null || true
        if [[ -f /usr/lib/binfmt.d/qemu-aarch64.conf ]]; then
            sudo sh -c 'cat /usr/lib/binfmt.d/qemu-aarch64.conf > /proc/sys/fs/binfmt_misc/register' 2>/dev/null || true
        fi
    fi
fi

log "=== x86_64 rootfs ==="
BACKROOT8_ARCH=x86_64 sudo -E "$ROOT/scripts/build-rootfs.sh" 2>&1 | tee -a "$LOG"

log "=== aarch64 rootfs ==="
BACKROOT8_ARCH=aarch64 sudo -E "$ROOT/scripts/build-rootfs.sh" 2>&1 | tee -a "$LOG"

ln -sfn backroot8-x86_64.img "$VM_DIR/backroot8.img"

log "Milestone 1 images:"
ls -lh "$VM_DIR"/backroot8-x86_64.img "$VM_DIR"/backroot8-aarch64.img "$VM_DIR"/backroot8.img 2>&1 | tee -a "$LOG"
log "Done. x86 VM: ./scripts/run-vm-gui.sh  |  aarch64: ./scripts/run-vm-aarch64.sh"
