#!/usr/bin/env bash
# Build a hybrid BIOS+UEFI bootable ISO for Backroot 8 (Milestone 1).
# Requires: build-rootfs.sh output (vm/backroot8.img), grub-mkrescue, xorriso.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VM_DIR="$ROOT/vm"
DISK="$VM_DIR/backroot8.img"
MNT="$VM_DIR/mnt"
ISO_STAGING="$VM_DIR/iso-staging"
ISO_OUT="${ISO_OUT:-$VM_DIR/backroot8-milestone1.iso}"
VERSION_TAG="${BACKROOT8_VERSION:-8-milestone1}"

log() { echo "[backroot8-iso] $*"; }

cleanup_mount() {
    mountpoint -q "$MNT" && sudo umount "$MNT" || true
}

trap cleanup_mount EXIT

if [[ ! -f "$DISK" ]]; then
    log "Disk image missing; running build-rootfs.sh first..."
    sudo "$ROOT/scripts/build-rootfs.sh"
fi

if ! sudo blkid "$DISK" | grep -q 'LABEL="backroot8"'; then
    log "Disk image has no LABEL=backroot8; rebuild with build-rootfs.sh" >&2
    exit 1
fi

log "Staging ISO contents (${VERSION_TAG})..."
rm -rf "$ISO_STAGING"
mkdir -p "$ISO_STAGING/boot/grub" "$MNT"
if ! mountpoint -q "$MNT"; then
    sudo mount -o loop,ro "$DISK" "$MNT"
fi

cp "$MNT/boot/vmlinuz-linux" "$ISO_STAGING/boot/vmlinuz-linux"
cp "$MNT/boot/initramfs-linux.img" "$ISO_STAGING/boot/initramfs-linux.img"

cat > "$ISO_STAGING/boot/grub/grub.cfg" <<'GRUB'
set default=0
set timeout=3

menuentry "Backroot 8 Milestone 1" {
    # Kernel/initrd on the ISO9660 volume; root on appended ext4 (LABEL=backroot8).
    if [ -f ($cd0)/boot/vmlinuz-linux ]; then
        set root=$cd0
    else
        search --no-floppy --file --set=root /boot/vmlinuz-linux
    fi
    linux /boot/vmlinuz-linux root=LABEL=backroot8 rw quiet loglevel=3
    initrd /boot/initramfs-linux.img
}
GRUB

sudo umount "$MNT"
trap - EXIT

log "Building hybrid ISO (this may take several minutes)..."
rm -f "$ISO_OUT"
grub-mkrescue \
    --compress=xz \
    --modules="part_msdos part_gpt ext2 search search_label normal linux boot" \
    -o "$ISO_OUT" \
    "$ISO_STAGING" \
    -- \
    -append_partition 2 0x83 "$DISK" \
    -appended_part_as_gpt \
    -V "BACKROOT8_${VERSION_TAG}"

BUILD_USER="${SUDO_USER:-$USER}"
if [[ -n "$BUILD_USER" && "$BUILD_USER" != "root" ]]; then
    sudo chown "$BUILD_USER:$BUILD_USER" "$ISO_OUT" 2>/dev/null || true
fi

log "ISO ready: $ISO_OUT ($(du -h "$ISO_OUT" | awk '{print $1}'))"
log "Write to USB: sudo dd if=$ISO_OUT of=/dev/sdX bs=4M status=progress conv=fsync"
log "Or boot in VM: $ROOT/scripts/verify-iso-boot.sh"
