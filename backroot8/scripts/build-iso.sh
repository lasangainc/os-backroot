#!/usr/bin/env bash
# Build a hybrid BIOS+UEFI bootable ISO for Backroot 8 (Milestone 1).
# Root is ext4 inside squashfs on the ISO; initramfs hook backroot8_iso mounts it.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VM_DIR="$ROOT/vm"
DISK="$VM_DIR/backroot8.img"
MNT="$VM_DIR/mnt"
ISO_STAGING="$VM_DIR/iso-staging"
ROOT_IMG="$VM_DIR/backroot8-root.img"
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

if ! command -v mksquashfs >/dev/null; then
    log "mksquashfs required (squashfs-tools package)" >&2
    exit 1
fi

log "Staging ISO contents (${VERSION_TAG})..."
rm -rf "$ISO_STAGING" "$ROOT_IMG"
mkdir -p "$ISO_STAGING/boot/grub" "$MNT"
if ! mountpoint -q "$MNT"; then
    sudo mount -o loop,ro "$DISK" "$MNT"
fi

cp "$MNT/boot/vmlinuz-linux" "$ISO_STAGING/boot/vmlinuz-linux"
cp "$MNT/boot/initramfs-linux.img" "$ISO_STAGING/boot/initramfs-linux.img"

log "Creating ext4 root image for squashfs wrap..."
# Size = used bytes + headroom (mkfs.ext4 -d needs room for metadata)
USED_MB="$(sudo du -sm "$MNT" | awk '{print $1}')"
IMG_MB=$(( USED_MB + 512 ))
rm -f "$ROOT_IMG"
sudo mkfs.ext4 -F -L backroot8 -d "$MNT" "$ROOT_IMG" "${IMG_MB}M"

log "Compressing root into squashfs..."
sudo mksquashfs "$ROOT_IMG" "$ISO_STAGING/backroot8-root.squashfs" \
    -comp zstd -Xcompression-level 15 -noappend
sudo rm -f "$ROOT_IMG"

cat > "$ISO_STAGING/boot/grub/grub.cfg" <<'GRUB'
set default=0
set timeout=3

menuentry "Backroot 8 Milestone 1" {
    if [ -f ($cd0)/boot/vmlinuz-linux ]; then
        set root=$cd0
    else
        search --no-floppy --file --set=root /boot/vmlinuz-linux
    fi
    linux /boot/vmlinuz-linux backroot8iso root=LABEL=backroot8 rootdelay=3 fsck.mode=skip rw quiet loglevel=3 console=ttyS0,115200 earlyprintk=ttyS0,115200
    initrd /boot/initramfs-linux.img
}
GRUB

sudo umount "$MNT"
trap - EXIT

log "Building hybrid ISO..."
rm -f "$ISO_OUT"
grub-mkrescue \
    --compress=xz \
    --modules="part_msdos part_gpt iso9660 normal linux boot search search_label" \
    -o "$ISO_OUT" \
    "$ISO_STAGING" \
    -partition_cyl_align off \
    -V "BACKROOT8_M1"

BUILD_USER="${SUDO_USER:-$USER}"
if [[ -n "$BUILD_USER" && "$BUILD_USER" != "root" ]]; then
    sudo chown "$BUILD_USER:$BUILD_USER" "$ISO_OUT" 2>/dev/null || true
fi

log "ISO ready: $ISO_OUT ($(du -h "$ISO_OUT" | awk '{print $1}'))"
log "Write to USB: sudo dd if=$ISO_OUT of=/dev/sdX bs=4M status=progress conv=fsync"
log "Or boot in VM: $ROOT/scripts/verify-iso-boot.sh"
