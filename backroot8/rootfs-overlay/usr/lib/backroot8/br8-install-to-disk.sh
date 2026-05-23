#!/bin/bash
# Install Backroot 8 from live session onto a whole disk (destructive).
# GPT layout: BIOS boot + EFI System Partition + ext4 root (UEFI and legacy BIOS).
set -euo pipefail

DISK="${1:-}"
STATUS_DIR="/run/br8-install"
STATUS_FILE="$STATUS_DIR/status"
TARGET="/mnt/br8-target"
ESP_MOUNT="$TARGET/boot/efi"
LOG="/run/br8-install/install.log"
ESP_SIZE_MIB=512

status() {
    local pct="$1"
    local msg="$2"
    mkdir -p "$STATUS_DIR"
    {
        echo "percent=$pct"
        echo "message=$msg"
    } >"$STATUS_FILE"
}

fail() {
    status 0 "Installation failed: $*"
    echo "error=$*" >>"$STATUS_FILE"
    exit 1
}

[[ -n "$DISK" && -b "$DISK" ]] || fail "invalid disk $DISK"
grep -q 'backroot8iso' /proc/cmdline || fail "not a live session"

exec > >(tee -a "$LOG") 2>&1

status 5 "Partitioning disk…"
wipefs -af "$DISK"
parted -s "$DISK" mklabel gpt
parted -s "$DISK" mkpart bios 1MiB 3MiB
parted -s "$DISK" set 1 bios_grub on
parted -s "$DISK" mkpart esp fat32 3MiB "${ESP_SIZE_MIB}MiB"
parted -s "$DISK" set 2 esp on
parted -s "$DISK" mkpart root ext4 "${ESP_SIZE_MIB}MiB" 100%
sleep 2
echo "[br8-install] partprobe $DISK"
timeout 60 partprobe "$DISK" 2>/dev/null || partprobe "$DISK" 2>/dev/null || true

ESP_PART="${DISK}2"
ROOT_PART="${DISK}3"
[[ -b "$ESP_PART" ]] || ESP_PART="${DISK}p2"
[[ -b "$ROOT_PART" ]] || ROOT_PART="${DISK}p3"
[[ -b "$ESP_PART" && -b "$ROOT_PART" ]] || fail "partition layout failed"

status 14 "Formatting EFI (vfat)…"
echo "[br8-install] mkfs.fat on $ESP_PART"
# FAT volume labels are limited to 11 characters (BACKROOT8EFI is too long).
mkfs.fat -F32 -n BACKROOT8 "$ESP_PART" || fail "mkfs.fat failed on $ESP_PART"
status 17 "Formatting root (ext4, may take a few minutes)…"
echo "[br8-install] mkfs.ext4 on $ROOT_PART"
mkfs.ext4 -F -L Backroot8 "$ROOT_PART" || fail "mkfs.ext4 failed on $ROOT_PART"
echo "[br8-install] format complete"

status 25 "Mounting target…"
mkdir -p "$TARGET" "$ESP_MOUNT"
mount "$ROOT_PART" "$TARGET"
mount "$ESP_PART" "$ESP_MOUNT"

status 35 "Copying system files…"
rsync -aAXH \
    --exclude=/dev/* \
    --exclude=/proc/* \
    --exclude=/sys/* \
    --exclude=/tmp/* \
    --exclude=/run/* \
    --exclude=/mnt/* \
    --exclude=/media/* \
    --exclude=/lost+found \
    --exclude=/swapfile \
    / "$TARGET/" || fail "rsync failed"

status 55 "Configuring system for disk boot…"
# Installed system must not use live-only initramfs hooks.
if [[ -f "$TARGET/etc/mkinitcpio.conf" ]]; then
    sed -i 's/ backroot8_iso//g; s/ backroot8_root//g; s/backroot8_iso //g; s/backroot8_root //g' \
        "$TARGET/etc/mkinitcpio.conf"
fi
rm -f "$TARGET/etc/fstab"
if command -v genfstab >/dev/null; then
    genfstab -U "$TARGET" >>"$TARGET/etc/fstab"
else
    ROOT_UUID="$(blkid -s UUID -o value "$ROOT_PART")"
    ESP_UUID="$(blkid -s UUID -o value "$ESP_PART")"
    {
        echo "UUID=$ROOT_UUID / ext4 defaults 0 1"
        echo "UUID=$ESP_UUID /boot/efi vfat defaults,umask=0077 0 2"
    } >>"$TARGET/etc/fstab"
fi

cat >"$TARGET/etc/default/grub" <<EOF
GRUB_TIMEOUT=3
GRUB_DISTRIBUTOR="Backroot 8"
GRUB_CMDLINE_LINUX_DEFAULT="quiet loglevel=3"
GRUB_CMDLINE_LINUX=""
GRUB_DISABLE_OS_PROBER=true
EOF

for _d in dev proc sys run; do
    mount --bind "/$_d" "$TARGET/$_d"
done

chroot_cmd() {
    if command -v arch-chroot >/dev/null; then
        arch-chroot "$TARGET" "$@"
    else
        chroot "$TARGET" "$@"
    fi
}

status 70 "Installing boot loaders…"
chroot_cmd /bin/bash -eux <<CHROOT
set -e
if [[ ! -f /boot/vmlinuz-linux ]]; then
    echo "missing /boot/vmlinuz-linux on target" >&2
    exit 1
fi
mkinitcpio -P
grub-install --target=i386-pc --force-extra-removable "$DISK"
grub-install --target=x86_64-efi --efi-directory=/boot/efi --bootloader-id=Backroot8 --recheck
grub-mkconfig -o /boot/grub/grub.cfg
CHROOT

for _d in run sys proc dev; do
    umount "$TARGET/$_d" 2>/dev/null || true
done

status 85 "Finishing first-run setup…"
mkdir -p "$TARGET/etc/backroot8"
touch "$TARGET/etc/backroot8/oobe-pending"
rm -f "$TARGET/etc/backroot8/oobe-complete"

# Remove live-only kernel parameter from installed GRUB menu.
sed -i 's/backroot8iso//g; s/  / /g' "$TARGET/boot/grub/grub.cfg" 2>/dev/null || true

status 95 "Syncing disks…"
sync
umount "$ESP_MOUNT" 2>/dev/null || true
umount "$TARGET" 2>/dev/null || true

status 100 "Done"
echo "done=1" >>"$STATUS_FILE"
