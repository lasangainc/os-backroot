#!/usr/bin/env bash
# Build a hybrid BIOS+UEFI bootable Backroot 8 live ISO (Milestone 1).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VM_DIR="$ROOT/vm"
ROOTFS="$VM_DIR/rootfs"
ISO_STAGING="$VM_DIR/iso-staging"
ROOT_IMG="$VM_DIR/backroot8-root.img"
ISO_OUT="${ISO_OUT:-$VM_DIR/backroot8-live.iso}"
VERSION_TAG="${BACKROOT8_VERSION:-8-milestone1}"

log() { echo "[backroot8-iso] $*"; }

need_host_tools() {
    local missing=()
    for cmd in mksquashfs grub-mkrescue; do
        command -v "$cmd" >/dev/null || missing+=("$cmd")
    done
    if [[ ${#missing[@]} -gt 0 ]]; then
        log "Missing host tools: ${missing[*]}"
        log "Run: sudo $ROOT/scripts/install-iso-build-deps.sh"
        exit 1
    fi
}

if [[ "$(id -u)" -ne 0 ]]; then
    exec sudo --preserve-env=ISO_OUT,BACKROOT8_VERSION "${BASH_SOURCE[0]}" "$@"
fi

need_host_tools

if [[ ! -f "$ROOTFS/boot/vmlinuz-linux" ]]; then
    log "Rootfs not found; building..."
    "$ROOT/scripts/build-root.sh"
fi

log "Staging ISO (${VERSION_TAG})..."
rm -rf "$ISO_STAGING" "$ROOT_IMG"
mkdir -p "$ISO_STAGING/boot/grub"

log "Trimming package caches (smaller ISO)..."
rm -rf "$ROOTFS/var/cache/pacman/pkg"/* \
    "$ROOTFS/usr/share/doc"/* \
    "$ROOTFS/usr/share/man"/* 2>/dev/null || true

cp "$ROOTFS/boot/vmlinuz-linux" "$ISO_STAGING/boot/vmlinuz-linux"
cp "$ROOTFS/boot/initramfs-linux.img" "$ISO_STAGING/boot/initramfs-linux.img"

USED_MB="$(du -sm "$ROOTFS" | awk '{print $1}')"
IMG_MB=$(( USED_MB + 512 ))
log "Creating ext4 root image (${IMG_MB}MB) for squashfs..."
mkfs.ext4 -F -L backroot8 -d "$ROOTFS" "$ROOT_IMG" "${IMG_MB}M"

log "Compressing root into squashfs..."
mksquashfs "$ROOT_IMG" "$ISO_STAGING/backroot8-root.squashfs" \
    -comp zstd -Xcompression-level 15 -noappend
rm -f "$ROOT_IMG"

cat > "$ISO_STAGING/boot/grub/grub.cfg" <<'GRUB'
set default=0
set timeout=3

menuentry "Backroot 8 Live" {
    if [ -f ($cd0)/boot/vmlinuz-linux ]; then
        set root=$cd0
    else
        search --no-floppy --file --set=root /boot/vmlinuz-linux
    fi
    linux /boot/vmlinuz-linux backroot8iso root=LABEL=backroot8 rootdelay=3 fsck.mode=skip rw quiet loglevel=3 console=ttyS0,115200 earlyprintk=ttyS0,115200
    initrd /boot/initramfs-linux.img
}
GRUB

log "Building hybrid ISO..."
rm -f "$ISO_OUT"
grub-mkrescue \
    --compress=xz \
    --modules="part_msdos part_gpt iso9660 normal linux boot search search_label" \
    -o "$ISO_OUT" \
    "$ISO_STAGING" \
    -partition_cyl_align off \
    -V "BACKROOT8_M1"

BUILD_USER="${SUDO_USER:-${USER:-root}}"
if [[ -n "$BUILD_USER" && "$BUILD_USER" != "root" ]]; then
    chown "$BUILD_USER:$BUILD_USER" "$ISO_OUT" 2>/dev/null || true
fi

log "ISO ready: $ISO_OUT ($(du -h "$ISO_OUT" | awk '{print $1}'))"
log "Test: $ROOT/scripts/verify-iso-boot.sh"
log "GUI:  $ROOT/scripts/run-vm-gui.sh"
log "USB:  sudo dd if=$ISO_OUT of=/dev/sdX bs=4M status=progress conv=fsync"
