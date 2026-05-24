#!/usr/bin/env bash
# Build Backroot 8 root filesystem directory (Arch pacstrap + overlay).
# Used as input for scripts/build-iso.sh — no VM disk image.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VM_DIR="$ROOT/vm"
ROOTFS="$VM_DIR/rootfs"
BOOTSTRAP="$VM_DIR/archlinux-bootstrap.tar.zst"
PACKAGES_FILE="$ROOT/packages.backroot8.txt"

log() { echo "[backroot8-root] $*"; }

cleanup_mounts() {
    sudo umount "$ROOTFS/var/cache/pacman/pkg" 2>/dev/null || true
    sudo umount "$ROOTFS" 2>/dev/null || true
    sudo umount -R "$ROOTFS/dev/pts" 2>/dev/null || true
    sudo umount -R "$ROOTFS/dev" 2>/dev/null || true
    sudo umount -R "$ROOTFS/proc" 2>/dev/null || true
    sudo umount -R "$ROOTFS/sys" 2>/dev/null || true
    sudo umount -R "$ROOTFS/run" 2>/dev/null || true
}

trap cleanup_mounts EXIT

if [[ "$(id -u)" -ne 0 ]]; then
    exec sudo "${BASH_SOURCE[0]}" "$@"
fi

if [[ ! -f "$BOOTSTRAP" ]]; then
    log "Downloading Arch bootstrap..."
    mkdir -p "$VM_DIR"
    curl -fsSL -o "$BOOTSTRAP" \
        "https://geo.mirror.pkgbuild.com/iso/latest/archlinux-bootstrap-x86_64.tar.zst"
fi

mkdir -p "$VM_DIR" "$ROOTFS"

if [[ ! -f "$ROOTFS/bin/bash" ]]; then
    log "Extracting Arch bootstrap into $ROOTFS..."
    rm -rf "${ROOTFS:?}"/*
    tar -xf "$BOOTSTRAP" -C "$ROOTFS" --strip-components=1
fi

log "Configuring pacman mirrors..."
sed -i 's/^#Server/Server/' "$ROOTFS/etc/pacman.d/mirrorlist" 2>/dev/null || true

sed -i 's/^#DisableSandbox.*/DisableSandbox/' "$ROOTFS/etc/pacman.conf" 2>/dev/null || true
grep -q '^DisableSandbox' "$ROOTFS/etc/pacman.conf" || \
    sed -i '/^\[options\]/a DisableSandbox' "$ROOTFS/etc/pacman.conf"

mount_if() { mountpoint -q "$2" || mount "$@"; }
mount_if --bind /dev "$ROOTFS/dev"
mount_if --bind /proc "$ROOTFS/proc"
mount_if --bind /sys "$ROOTFS/sys"
mount_if --bind /run "$ROOTFS/run"
mkdir -p "$ROOTFS/dev/pts"
mount_if -t devpts devpts "$ROOTFS/dev/pts"
[[ -f /etc/resolv.conf ]] && cp /etc/resolv.conf "$ROOTFS/etc/resolv.conf"

PKG_CACHE="/tmp/backroot8-pacman-pkg"
mkdir -p "$PKG_CACHE" "$ROOTFS/var/cache/pacman/pkg"
mount_if --bind "$PKG_CACHE" "$ROOTFS/var/cache/pacman/pkg"
# pacman requires / to be a mount point for free-space checks
mount_if --bind "$ROOTFS" "$ROOTFS"

mapfile -t BR8_PACKAGES < <(grep -vE '^\s*($|#)' "$PACKAGES_FILE")

log "Installing Arch packages (${#BR8_PACKAGES[@]} entries)..."
arch-chroot "$ROOTFS" /bin/bash -eux <<CHROOT
mkdir -p /var/cache/pacman/pkg
pacman-key --init
pacman-key --populate archlinux
pacman -Sy --noconfirm
pacman -S --noconfirm --needed --disable-download-timeout ${BR8_PACKAGES[*]}

echo "en_US.UTF-8 UTF-8" >> /etc/locale.gen
locale-gen
echo "LANG=en_US.UTF-8" > /etc/locale.conf
ln -sf /usr/share/zoneinfo/UTC /etc/localtime

echo "root:backroot8" | chpasswd
passwd -u root

echo "backroot8" > /etc/hostname
cat > /etc/hosts <<EOF
127.0.0.1   localhost
::1         localhost
127.0.1.1   backroot8.localdomain backroot8
EOF

sed -i 's/^#*PermitRootLogin.*/PermitRootLogin yes/' /etc/ssh/sshd_config
sed -i 's/^#*PasswordAuthentication.*/PasswordAuthentication yes/' /etc/ssh/sshd_config
grep -q '^PermitRootLogin yes' /etc/ssh/sshd_config || echo 'PermitRootLogin yes' >> /etc/ssh/sshd_config
grep -q '^PasswordAuthentication yes' /etc/ssh/sshd_config || echo 'PasswordAuthentication yes' >> /etc/ssh/sshd_config

ssh-keygen -A
chown root:root /usr/share/empty.sshd
chmod 755 /usr/share/empty.sshd

systemctl enable NetworkManager
systemctl enable sshd

# tty1 is owned by backroot8-desktop (startx); avoid getty fighting Plymouth/X.
systemctl mask getty@tty1.service

sed -i 's/^HOOKS=.*/HOOKS=(base udev modconf kms block plymouth backroot8_iso backroot8_root filesystems fsck)/' /etc/mkinitcpio.conf
grep -q '^MODULES=.*overlay' /etc/mkinitcpio.conf || \
    sed -i 's/^MODULES=(/MODULES=(overlay /' /etc/mkinitcpio.conf
pacman -Syu --noconfirm
CHROOT

log "Installing Backroot overlay and binaries..."
"$ROOT/scripts/sync-overlay.sh" "$ROOTFS"

log "Installing Segoe UI font..."
SEGOE_TMP="$(mktemp -d)"
curl -fsSL -o "$SEGOE_TMP/segoe-ui-variable.zip" "https://aka.ms/SegoeUIVariable"
unzip -q "$SEGOE_TMP/segoe-ui-variable.zip" -d "$SEGOE_TMP/extract"
mkdir -p "$ROOTFS/usr/share/fonts/segoe-ui" "$ROOTFS/usr/share/licenses/segoe-ui-variable"
install -Dm644 "$SEGOE_TMP/extract"/Segoe*.ttf "$ROOTFS/usr/share/fonts/segoe-ui/"
install -Dm644 "$SEGOE_TMP/extract/EULA.txt" \
    "$ROOTFS/usr/share/licenses/segoe-ui-variable/LICENSE"
arch-chroot "$ROOTFS" fc-cache -f
rm -rf "$SEGOE_TMP"

arch-chroot "$ROOTFS" plymouth-set-default-theme -R backroot8

arch-chroot "$ROOTFS" systemctl enable \
    plymouth-start.service backroot8-live-cow.service backroot8-desktop.service sshd \
    2>/dev/null || true

mkdir -p "$ROOTFS/root"
cat > "$ROOTFS/root/.xinitrc" <<'EOF'
#!/bin/sh
exec /etc/X11/xinit/xinitrc
EOF
chmod +x "$ROOTFS/root/.xinitrc"

log "Regenerating initramfs with backroot8_iso hook..."
arch-chroot "$ROOTFS" mkinitcpio -P
if ! arch-chroot "$ROOTFS" lsinitcpio -l /boot/initramfs-linux.img | grep -q 'kernel/fs/overlayfs/overlay.ko'; then
    log "ERROR: overlay module missing from initramfs (check mkinitcpio hooks)"
    exit 1
fi
if ! arch-chroot "$ROOTFS" lsinitcpio -l /boot/initramfs-linux.img | grep -q '^usr/bin/mount$'; then
    log "ERROR: util-linux mount missing from initramfs (overlay needs it)"
    exit 1
fi

cleanup_mounts
trap - EXIT

BUILD_USER="${SUDO_USER:-${USER:-root}}"
if [[ -n "$BUILD_USER" && "$BUILD_USER" != "root" ]]; then
    chown -R "$BUILD_USER:$BUILD_USER" "$ROOTFS" "$BOOTSTRAP" 2>/dev/null || true
fi

log "Root filesystem ready: $ROOTFS"
