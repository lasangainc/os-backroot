#!/usr/bin/env bash
# Build Backroot 8 root filesystem on a disk image (Arch + precompiled kernel)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VM_DIR="$ROOT/vm"
DISK="$VM_DIR/backroot8.img"
BOOTSTRAP="$VM_DIR/archlinux-bootstrap.tar.zst"
MNT="$VM_DIR/mnt"
SIZE_MB="${DISK_SIZE_MB:-6144}"

log() { echo "[backroot8] $*"; }

cleanup_mounts() {
    sudo umount -R "$MNT/dev/pts" 2>/dev/null || true
    sudo umount -R "$MNT/dev" 2>/dev/null || true
    sudo umount -R "$MNT/proc" 2>/dev/null || true
    sudo umount -R "$MNT/sys" 2>/dev/null || true
    sudo umount -R "$MNT/run" 2>/dev/null || true
    sudo umount "$MNT/boot" 2>/dev/null || true
    sudo umount "$MNT" 2>/dev/null || true
    sudo losetup -D 2>/dev/null || true
}

trap cleanup_mounts EXIT

log "Building binaries..."
make -C "$ROOT/src/br8-wm" clean br8-wm
make -C "$ROOT/src/br8-panel" clean br8-panel
make -C "$ROOT/src/backroot-hello" clean backroot-hello

if [[ ! -f "$BOOTSTRAP" ]]; then
    log "Downloading Arch bootstrap..."
    curl -fsSL -o "$BOOTSTRAP" "https://geo.mirror.pkgbuild.com/iso/latest/archlinux-bootstrap-x86_64.tar.zst"
fi

log "Creating disk image (${SIZE_MB}MB)..."
mkdir -p "$VM_DIR" "$MNT"
if [[ ! -f "$DISK" ]]; then
    truncate -s "${SIZE_MB}M" "$DISK"
fi

if [[ ! -f "$DISK" ]]; then
    truncate -s "${SIZE_MB}M" "$DISK"
fi

if ! sudo blkid "$DISK" | grep -q ext4; then
    log "Formatting whole disk as ext4..."
    sudo mkfs.ext4 -F -L backroot8 "$DISK"
fi

log "Mounting root..."
if ! mountpoint -q "$MNT"; then
    sudo mount -o loop "$DISK" "$MNT"
fi
LOOP=$(findmnt -n -o SOURCE "$MNT" | sed 's/p[0-9]*$//')

if [[ ! -f "$MNT/bin/bash" ]]; then
    log "Extracting Arch bootstrap..."
    sudo tar -xf "$BOOTSTRAP" -C "$MNT" --strip-components=1
fi

log "Configuring pacman mirrors..."
sudo sed -i 's/^#Server/Server/' "$MNT/etc/pacman.d/mirrorlist" 2>/dev/null || true

log "Chroot setup..."
mount_if() { mountpoint -q "$2" || sudo mount "$@"; }
mount_if --bind /dev "$MNT/dev"
mount_if --bind /proc "$MNT/proc"
mount_if --bind /sys "$MNT/sys"
mount_if --bind /run "$MNT/run"
sudo mkdir -p "$MNT/dev/pts"
mount_if -t devpts devpts "$MNT/dev/pts"
[[ -f /etc/resolv.conf ]] && sudo cp /etc/resolv.conf "$MNT/etc/resolv.conf"

# Landlock sandbox fails in some container/chroot hosts
sudo sed -i 's/^#DisableSandbox.*/DisableSandbox/' "$MNT/etc/pacman.conf" 2>/dev/null || true
grep -q '^DisableSandbox' "$MNT/etc/pacman.conf" || \
    sudo sed -i '/^\[options\]/a DisableSandbox' "$MNT/etc/pacman.conf"

sudo arch-chroot "$MNT" /bin/bash -eux <<'CHROOT'
pacman-key --init
pacman-key --populate archlinux
pacman -Sy --noconfirm

# Precompiled Arch kernel (linux package)
pacman -S --noconfirm --needed \
    linux linux-firmware \
    base base-devel \
    xorg-server xorg-xinit xorg-xrandr xf86-video-vesa \
    xterm dolphin feh nettle xorg-fonts-misc libxft ttf-dejavu gtk4 \
    systemd-sysvcompat \
    sudo networkmanager \
    mkinitcpio grub efibootmgr \
    dhcpcd openssh

# Locale and time
echo "en_US.UTF-8 UTF-8" >> /etc/locale.gen
locale-gen
echo "LANG=en_US.UTF-8" > /etc/locale.conf
ln -sf /usr/share/zoneinfo/UTC /etc/localtime

# Root login for VM
echo "root:backroot8" | chpasswd
passwd -u root

# Hostname
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

systemctl enable NetworkManager
systemctl enable sshd
systemctl enable backroot8-desktop.service 2>/dev/null || true
mkdir -p /etc/systemd/system/getty@tty1.service.d
cat > /etc/systemd/system/getty@tty1.service.d/autologin.conf <<'AUTO'
[Service]
ExecStart=
ExecStart=-/usr/bin/agetty --autologin root --noclear %I $TERM
AUTO

# mkinitcpio and grub
sed -i 's/^HOOKS=.*/HOOKS=(base udev modconf block filesystems fsck)/' /etc/mkinitcpio.conf
mkinitcpio -P

mkdir -p /boot/grub
cat > /boot/grub/grub.cfg <<'GRUB'
set default=0
set timeout=2
menuentry "Backroot 8" {
    set root=(hd0)
    linux /boot/vmlinuz-linux root=LABEL=backroot8 rw quiet
    initrd /boot/initramfs-linux.img
}
GRUB
# Sync all packages so terminals match current nettle/gnutls SONAMEs
pacman -Syu --noconfirm
CHROOT

log "Installing Backroot 8 desktop..."
sudo install -Dm755 "$ROOT/src/br8-wm/br8-wm" "$MNT/usr/local/bin/br8-wm"
sudo install -Dm755 "$ROOT/src/br8-panel/br8-panel" "$MNT/usr/local/bin/br8-panel"
sudo install -Dm755 "$ROOT/src/backroot-hello/backroot-hello" "$MNT/usr/local/bin/backroot-hello"
sudo install -Dm644 "$ROOT/rootfs-overlay/usr/share/backroot/backroot-hello/backroot-hello.css" \
    "$MNT/usr/share/backroot/backroot-hello/backroot-hello.css"
sudo install -Dm644 "$ROOT/rootfs-overlay/usr/share/backroot/README" \
    "$MNT/usr/share/backroot/README"
sudo install -Dm644 "$ROOT/rootfs-overlay/usr/share/applications/backroot-hello.desktop" \
    "$MNT/usr/share/applications/backroot-hello.desktop"

sudo install -Dm755 "$ROOT/rootfs-overlay/etc/X11/xinit/xinitrc" "$MNT/etc/X11/xinit/xinitrc"
sudo install -Dm644 "$ROOT/rootfs-overlay/usr/share/backgrounds/backroot8.jpg" \
    "$MNT/usr/share/backgrounds/backroot8.jpg"
sudo install -Dm644 "$ROOT/rootfs-overlay/etc/profile.d/backroot8.sh" "$MNT/etc/profile.d/backroot8.sh"
sudo install -Dm644 "$ROOT/rootfs-overlay/etc/motd" "$MNT/etc/motd"
sudo install -Dm644 "$ROOT/rootfs-overlay/etc/systemd/system/backroot8-desktop.service" \
    "$MNT/etc/systemd/system/backroot8-desktop.service"
sudo arch-chroot "$MNT" systemctl enable backroot8-desktop.service sshd 2>/dev/null || true

sudo mkdir -p "$MNT/root"
cat <<'EOF' | sudo tee "$MNT/root/.xinitrc" >/dev/null
#!/bin/sh
exec /etc/X11/xinit/xinitrc
EOF
sudo chmod +x "$MNT/root/.xinitrc"

# Auto-start X on login
cat <<'EOF' | sudo tee "$MNT/root/.bash_profile" >/dev/null
[[ -z $DISPLAY && $XDG_VTNR -eq 1 ]] && exec startx
EOF

# Install grub to disk from host after chroot
LOOP_DEV="${LOOP:-$(losetup -j "$DISK" 2>/dev/null | awk -F: '{print $1}' | head -1)}"
if [[ -n "$LOOP_DEV" && -b "$LOOP_DEV" ]]; then
    log "Installing GRUB to $LOOP_DEV..."
    sudo grub-install --target=i386-pc --boot-directory="$MNT/boot" --recheck "$LOOP_DEV"
fi

cleanup_mounts
trap - EXIT

# QEMU in cloud agents must read the image as the build user
BUILD_USER="${SUDO_USER:-$USER}"
if [[ -n "$BUILD_USER" && "$BUILD_USER" != "root" ]]; then
    sudo chown "$BUILD_USER:$BUILD_USER" "$DISK" 2>/dev/null || true
fi

log "Rootfs build complete: $DISK"
