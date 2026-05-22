#!/usr/bin/env bash
# Build a bootable Backroot 8 x86_64 live ISO from the disk image (Arch live-style).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VM_DIR="$ROOT/vm"
DISK="${DISK:-$VM_DIR/backroot8-x86_64.img}"
MNT="$VM_DIR/mnt"
ISO_WORK="$VM_DIR/iso-work"
ISO_OUT="$VM_DIR/backroot8-x86_64.iso"
ISO_LABEL="${ISO_LABEL:-BACKROOT8_M1}"
ARCH_DIR="arch"

log() { echo "[backroot8:iso] $*"; }

cleanup_mounts() {
    sudo umount -R "$MNT/dev/pts" 2>/dev/null || true
    sudo umount -R "$MNT/dev" 2>/dev/null || true
    sudo umount -R "$MNT/proc" 2>/dev/null || true
    sudo umount -R "$MNT/sys" 2>/dev/null || true
    sudo umount -R "$MNT/run" 2>/dev/null || true
    sudo umount "$MNT" 2>/dev/null || true
}

trap cleanup_mounts EXIT

if [[ ! -f "$DISK" ]]; then
    log "Disk image not found: $DISK"
    log "Run: sudo BACKROOT8_ARCH=x86_64 ./scripts/build-rootfs.sh"
    exit 1
fi

for cmd in mksquashfs xorriso grub-mkrescue; do
    command -v "$cmd" >/dev/null || { log "Missing $cmd"; exit 1; }
done

log "Mounting $DISK..."
mkdir -p "$MNT"
if ! mountpoint -q "$MNT"; then
    sudo mount -o loop "$DISK" "$MNT"
fi

sudo rm -f "$MNT/var/lib/pacman/db.lck" 2>/dev/null || true

log "Installing archiso in guest (for live initramfs hooks)..."
mount_if() { mountpoint -q "$2" || sudo mount "$@"; }
mount_if --bind /dev "$MNT/dev"
mount_if --bind /proc "$MNT/proc"
mount_if --bind /sys "$MNT/sys"
mount_if --bind /run "$MNT/run"
sudo mkdir -p "$MNT/dev/pts"
mount_if -t devpts devpts "$MNT/dev/pts"

if [[ -f /etc/resolv.conf ]]; then
    sudo rm -f "$MNT/etc/resolv.conf"
    sudo cp /etc/resolv.conf "$MNT/etc/resolv.conf"
fi

sudo arch-chroot "$MNT" /bin/bash -eux <<'CHROOT'
pacman -Sy --noconfirm
pacman -S --noconfirm --needed mkinitcpio-archiso
CHROOT

log "Preparing ISO workspace..."
rm -rf "$ISO_WORK"
mkdir -p "$ISO_WORK/$ARCH_DIR/boot/x86_64" "$ISO_WORK/boot/grub"

MKINITCPIO_ISO="$ISO_WORK/mkinitcpio-iso.conf"
cat >"$MKINITCPIO_ISO" <<'EOF'
HOOKS=(base udev modconf block filesystems keyboard archiso)
EOF

log "Building live initramfs (archiso hooks)..."
sudo cp -f "$MKINITCPIO_ISO" "$MNT/mkinitcpio-iso.conf"
sudo mkdir -p "$MNT/iso"
sudo arch-chroot "$MNT" env MKINITCPIO_CONF=/mkinitcpio-iso.conf /bin/bash -eux <<'CHROOT'
linuxver=$(ls /usr/lib/modules | sort -V | tail -1)
mkinitcpio -c "$MKINITCPIO_CONF" -g /iso/initramfs-linux.img -k "$linuxver"
CHROOT
sudo rm -f "$MNT/mkinitcpio-iso.conf"

sudo cp -f "$MNT/boot/vmlinuz-linux" "$ISO_WORK/$ARCH_DIR/boot/x86_64/vmlinuz-linux"
sudo cp -f "$MNT/iso/initramfs-linux.img" "$ISO_WORK/$ARCH_DIR/boot/x86_64/initramfs-linux.img"
sudo rm -f "$MNT/iso/initramfs-linux.img"

log "Creating squashfs root (this takes a few minutes)..."
mkdir -p "$ISO_WORK/$ARCH_DIR/x86_64"
sudo mksquashfs "$MNT" "$ISO_WORK/$ARCH_DIR/x86_64/airootfs.squashfs" \
    -noappend -comp zstd -Xcompression-level 19 \
    -e boot \
    -e dev \
    -e proc \
    -e sys \
    -e run \
    -e tmp \
    -e mnt \
    -e iso-work \
    -e var/cache/pacman/pkg \
    -e var/log \
    -e lost+found \
    -e "$ARCH_DIR"

cat >"$ISO_WORK/boot/grub/grub.cfg" <<EOF
set timeout=5
default=0

menuentry "Backroot 8 Milestone 1 (live)" {
    set gfxpayload=keep
    linux /${ARCH_DIR}/boot/x86_64/vmlinuz-linux archisobasedir=${ARCH_DIR} archisolabel=${ISO_LABEL} cow_spacesize=1G
    initrd /${ARCH_DIR}/boot/x86_64/initramfs-linux.img
}

menuentry "Backroot 8 (live, safe graphics)" {
    set gfxpayload=keep
    linux /${ARCH_DIR}/boot/x86_64/vmlinuz-linux archisobasedir=${ARCH_DIR} archisolabel=${ISO_LABEL} cow_spacesize=1G nomodeset
    initrd /${ARCH_DIR}/boot/x86_64/initramfs-linux.img
}
EOF

log "Assembling hybrid ISO with grub-mkrescue..."
command -v mformat >/dev/null || { log "Install mtools (mformat) for grub-mkrescue"; exit 1; }
sudo chown -R "$(id -un):$(id -gn)" "$ISO_WORK"
rm -f "$ISO_OUT"
grub-mkrescue -o "$ISO_OUT" "$ISO_WORK" \
    -volid "$ISO_LABEL" \
    -iso-level 3 \
    -full-iso9660-filenames \
    -joliet -joliet-long \
    -cache-inodes

cleanup_mounts
trap - EXIT

ls -lh "$ISO_OUT"
log "Bootable ISO ready: $ISO_OUT"
log "UTM/mac: attach ISO as CD, boot Legacy BIOS (UEFI off), or pick CD in boot menu."
