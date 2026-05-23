#!/bin/bash
# Install Backroot 8 from live session onto a whole disk (destructive).
set -euo pipefail

DISK="${1:-}"
STATUS_DIR="/run/br8-install"
STATUS_FILE="$STATUS_DIR/status"
TARGET="/mnt/br8-target"
LOG="/run/br8-install/install.log"

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

status 5 "Preparing disk…"
wipefs -af "$DISK"
parted -s "$DISK" mklabel msdos
parted -s "$DISK" mkpart primary ext4 1MiB 100%
parted -s "$DISK" set 1 boot on
sleep 1
partprobe "$DISK" 2>/dev/null || true
PART="${DISK}1"
[[ -b "$PART" ]] || PART="${DISK}p1"
[[ -b "$PART" ]] || fail "partition not found"

status 15 "Formatting…"
mkfs.ext4 -F -L Backroot8 "$PART"

status 25 "Mounting target…"
mkdir -p "$TARGET"
mount "$PART" "$TARGET"

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

status 70 "Configuring boot loader…"
if command -v genfstab >/dev/null; then
    genfstab -U "$TARGET" >>"$TARGET/etc/fstab"
else
    UUID="$(blkid -s UUID -o value "$PART")"
    echo "UUID=$UUID / ext4 defaults 0 1" >>"$TARGET/etc/fstab"
fi

cat >"$TARGET/etc/default/grub" <<EOF
GRUB_TIMEOUT=3
GRUB_DISTRIBUTOR="Backroot 8"
GRUB_CMDLINE_LINUX_DEFAULT="quiet loglevel=3"
GRUB_CMDLINE_LINUX=""
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

chroot_cmd /bin/bash -eux <<CHROOT
grub-install --target=i386-pc "$DISK"
grub-mkconfig -o /boot/grub/grub.cfg
CHROOT

for _d in run sys proc dev; do
    umount "$TARGET/$_d" 2>/dev/null || true
done

status 85 "Finishing first-run setup…"
mkdir -p "$TARGET/etc/backroot8"
touch "$TARGET/etc/backroot8/oobe-pending"
rm -f "$TARGET/etc/backroot8/oobe-complete"

# Drop live-only kernel parameter from installed GRUB entries.
sed -i 's/backroot8iso//g' "$TARGET/boot/grub/grub.cfg" 2>/dev/null || true

status 95 "Syncing disks…"
sync

status 100 "Done"
echo "done=1" >>"$STATUS_FILE"
sleep 2
systemctl reboot
