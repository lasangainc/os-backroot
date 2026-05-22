#!/usr/bin/env bash
# Build Backroot 8 root filesystem on a disk image (Arch + precompiled kernel)
# BACKROOT8_ARCH=x86_64 (default) or aarch64
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ARCH="${BACKROOT8_ARCH:-x86_64}"
case "$ARCH" in
    x86_64|aarch64) ;;
    *)
        echo "Unsupported BACKROOT8_ARCH=$ARCH (use x86_64 or aarch64)" >&2
        exit 1
        ;;
esac

VM_DIR="$ROOT/vm"
DISK="$VM_DIR/backroot8-${ARCH}.img"
BOOTSTRAP="$VM_DIR/archlinux-bootstrap-${ARCH}.tar.zst"
ALARM_ROOTFS="$VM_DIR/ArchLinuxARM-aarch64-latest.tar.gz"
PACMAN_ALARM_CONF="$VM_DIR/pacman-alarm-aarch64.conf"
MNT="$VM_DIR/mnt"
SIZE_MB="${DISK_SIZE_MB:-6144}"
HOST_ARCH="$(uname -m)"
NATIVE_BUILD=0
[[ "$ARCH" == "$HOST_ARCH" ]] && NATIVE_BUILD=1

if [[ "$ARCH" == "aarch64" ]]; then
    XORG_DRV=xf86-video-fbdev
    GRUB_TARGET=arm64-efi
    KERNEL_PKG=linux-aarch64
    VMLINUZ=vmlinuz-linux-aarch64
    INITRD_IMG=initramfs-linux-aarch64.img
    USE_ALARM=1
else
    XORG_DRV=xf86-video-vesa
    GRUB_TARGET=i386-pc
    KERNEL_PKG=linux
    VMLINUZ=vmlinuz-linux
    INITRD_IMG=initramfs-linux.img
    USE_ALARM=0
fi

log() { echo "[backroot8:$ARCH] $*"; }

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

ensure_aarch64_binfmt() {
    if [[ -f /proc/sys/fs/binfmt_misc/qemu-aarch64 ]]; then
        return 0
    fi
    if [[ ! -f /proc/sys/fs/binfmt_misc/register ]]; then
        sudo mount -t binfmt_misc binfmt_misc /proc/sys/fs/binfmt_misc 2>/dev/null || true
    fi
    if [[ -f /usr/lib/binfmt.d/qemu-aarch64.conf ]]; then
        log "Registering qemu-aarch64 binfmt for cross-arch chroot..."
        sudo sh -c 'cat /usr/lib/binfmt.d/qemu-aarch64.conf > /proc/sys/fs/binfmt_misc/register' 2>/dev/null || true
    fi
}

setup_aarch64_chroot() {
    ensure_aarch64_binfmt
    local qemu_static="/usr/bin/qemu-aarch64-static"
    if [[ ! -f "$qemu_static" ]]; then
        log "qemu-aarch64-static required for aarch64 chroot on $HOST_ARCH host"
        exit 1
    fi
    sudo mkdir -p "$MNT/usr/bin"
    sudo cp -f "$qemu_static" "$MNT/usr/bin/qemu-aarch64-static"
    sudo chmod 755 "$MNT/usr/bin/qemu-aarch64-static"
}

write_alarm_pacman_conf() {
    cat >"$PACMAN_ALARM_CONF" <<'EOF'
[options]
HoldPkg     = pacman glibc
Architecture = aarch64
DisableSandbox
CheckSpace
SigLevel    = Required DatabaseOptional
LocalFileSigLevel = Optional

[core]
Server = http://mirror.archlinuxarm.org/$arch/$repo

[extra]
Server = http://mirror.archlinuxarm.org/$arch/$repo

[alarm]
Server = http://mirror.archlinuxarm.org/$arch/$repo

[aur]
Server = http://mirror.archlinuxarm.org/$arch/$repo
EOF
}

bootstrap_alarm_root() {
    if [[ ! -f "$ALARM_ROOTFS" ]]; then
        log "Downloading Arch Linux ARM aarch64 rootfs (large, one-time)..."
        curl -fL --retry 3 -o "$ALARM_ROOTFS" \
            "http://os.archlinuxarm.org/os/ArchLinuxARM-aarch64-latest.tar.gz"
    fi
    log "Extracting Arch Linux ARM rootfs..."
    setup_aarch64_chroot
    sudo tar -xzf "$ALARM_ROOTFS" -C "$MNT"
    if [[ -d "$MNT/ArchLinuxARM-aarch64-latest" ]]; then
        sudo mv "$MNT/ArchLinuxARM-aarch64-latest"/* "$MNT/"
        sudo rmdir "$MNT/ArchLinuxARM-aarch64-latest" 2>/dev/null || \
            sudo rm -rf "$MNT/ArchLinuxARM-aarch64-latest"
    fi
}

build_binaries_on_host() {
    log "Building binaries on host ($HOST_ARCH)..."
    make -C "$ROOT/src/br8-wm" clean br8-wm
    make -C "$ROOT/src/br8-panel" clean br8-panel
    make -C "$ROOT/src/br8-start" clean br8-start
    make -C "$ROOT/src/backroot-hello" clean backroot-hello
    make -C "$ROOT/src/power-pdf" clean powerpdf
}

build_binaries_in_chroot() {
    log "Building binaries inside $ARCH chroot..."
    local bdir="/tmp/backroot8-build"
    sudo rm -rf "$MNT$bdir"
    sudo mkdir -p "$MNT$bdir"
    sudo cp -a "$ROOT/src" "$ROOT/include" "$ROOT/assets" "$MNT$bdir/"
    sudo cp "$ROOT/scripts/prepare-neoemblem.py" "$MNT$bdir/"
    sudo cp "$ROOT/assets/neoemblem.txt" "$MNT$bdir/assets/" 2>/dev/null || true
    sudo arch-chroot "$MNT" /bin/bash -eux <<BUILD
set -e
cd $bdir
make -C src/br8-panel emblem.h
make -C src/br8-wm clean br8-wm
make -C src/br8-panel clean br8-panel
make -C src/br8-start clean br8-start
make -C src/backroot-hello clean backroot-hello
make -C src/power-pdf clean powerpdf
BUILD
    BR8_WM="$MNT$bdir/src/br8-wm/br8-wm"
    BR8_PANEL="$MNT$bdir/src/br8-panel/br8-panel"
    BR8_START="$MNT$bdir/src/br8-start/br8-start"
    BR8_HELLO="$MNT$bdir/src/backroot-hello/backroot-hello"
    POWERPDF="$MNT$bdir/src/power-pdf/powerpdf"
    for f in "$BR8_WM" "$BR8_PANEL" "$BR8_START" "$BR8_HELLO" "$POWERPDF"; do
        [[ -f "$f" ]] || { log "Missing chroot build artifact: $f"; exit 1; }
    done
    HOST_WM="$ROOT/src/br8-wm/br8-wm"
    HOST_PANEL="$ROOT/src/br8-panel/br8-panel"
    HOST_START="$ROOT/src/br8-start/br8-start"
    HOST_HELLO="$ROOT/src/backroot-hello/backroot-hello"
    HOST_PDF="$ROOT/src/power-pdf/powerpdf"
    sudo cp -f "$BR8_WM" "$HOST_WM"
    sudo cp -f "$BR8_PANEL" "$HOST_PANEL"
    sudo cp -f "$BR8_START" "$HOST_START"
    sudo cp -f "$BR8_HELLO" "$HOST_HELLO"
    sudo cp -f "$POWERPDF" "$HOST_PDF"
    sudo chown "$(id -un):$(id -gn)" "$HOST_WM" "$HOST_PANEL" "$HOST_START" "$HOST_HELLO" "$HOST_PDF" 2>/dev/null || true
    sudo rm -rf "$MNT$bdir"
}

install_backroot_overlay() {
    log "Installing Backroot 8 desktop..."
    sudo install -Dm755 "$ROOT/src/br8-wm/br8-wm" "$MNT/usr/local/bin/br8-wm"
    sudo install -Dm755 "$ROOT/src/br8-panel/br8-panel" "$MNT/usr/local/bin/br8-panel"
    sudo install -Dm755 "$ROOT/src/br8-start/br8-start" "$MNT/usr/local/bin/br8-start"
    sudo install -Dm755 "$ROOT/src/backroot-hello/backroot-hello" "$MNT/usr/local/bin/backroot-hello"
    sudo install -Dm755 "$ROOT/src/power-pdf/powerpdf" "$MNT/usr/local/bin/powerpdf"
    sudo install -Dm644 "$ROOT/rootfs-overlay/usr/share/backroot/README" \
        "$MNT/usr/share/backroot/README"
    sudo install -Dm644 "$ROOT/rootfs-overlay/usr/share/applications/backroot-hello.desktop" \
        "$MNT/usr/share/applications/backroot-hello.desktop"
    sudo install -Dm644 "$ROOT/rootfs-overlay/usr/share/applications/powerpdf.desktop" \
        "$MNT/usr/share/applications/powerpdf.desktop"

    sudo install -Dm755 "$ROOT/rootfs-overlay/etc/X11/xinit/xinitrc" "$MNT/etc/X11/xinit/xinitrc"
    sudo install -Dm644 "$ROOT/rootfs-overlay/usr/share/backgrounds/backroot8.jpg" \
        "$MNT/usr/share/backgrounds/backroot8.jpg"
    sudo install -Dm644 "$ROOT/rootfs-overlay/usr/share/backroot8/bootscreen.png" \
        "$MNT/usr/share/backroot8/bootscreen.png"
    sudo install -Dm755 "$ROOT/rootfs-overlay/usr/share/backroot8/show-splash.sh" \
        "$MNT/usr/share/backroot8/show-splash.sh"
    sudo install -Dm755 "$ROOT/rootfs-overlay/usr/share/backroot8/br8-panel-launcher.sh" \
        "$MNT/usr/share/backroot8/br8-panel-launcher.sh"
    sudo install -Dm644 "$ROOT/rootfs-overlay/etc/profile.d/backroot8.sh" "$MNT/etc/profile.d/backroot8.sh"
    sudo install -Dm644 "$ROOT/rootfs-overlay/etc/profile.d/backroot8-neofetch.sh" \
        "$MNT/etc/profile.d/backroot8-neofetch.sh"
    sudo install -Dm644 "$ROOT/rootfs-overlay/etc/os-release" "$MNT/etc/os-release"
    sudo install -Dm644 "$ROOT/rootfs-overlay/etc/os-release" "$MNT/usr/lib/os-release"
    sudo install -Dm644 "$ROOT/assets/neoemblem.txt" "$MNT/usr/share/backroot8/neoemblem.txt"
    sudo install -Dm644 "$ROOT/rootfs-overlay/etc/neofetch/config.conf" "$MNT/etc/neofetch/config.conf"
    sudo mkdir -p "$MNT/root/.config/neofetch"
    sudo install -Dm644 "$ROOT/rootfs-overlay/etc/neofetch/config.conf" "$MNT/root/.config/neofetch/config.conf"
    sudo install -Dm755 "$ROOT/rootfs-overlay/usr/local/bin/neofetch" "$MNT/usr/local/bin/neofetch"
    sudo install -Dm644 "$ROOT/rootfs-overlay/etc/motd" "$MNT/etc/motd"
    sudo install -Dm644 "$ROOT/rootfs-overlay/etc/systemd/system/backroot8-desktop.service" \
        "$MNT/etc/systemd/system/backroot8-desktop.service"
    sudo install -Dm644 "$ROOT/rootfs-overlay/etc/systemd/system/backroot8-splash.service" \
        "$MNT/etc/systemd/system/backroot8-splash.service"
    sudo install -Dm644 "$ROOT/rootfs-overlay/etc/fonts/conf.d/99-segoe-ui.conf" \
        "$MNT/etc/fonts/conf.d/99-segoe-ui.conf"
}

log "Preparing branding assets..."
"$ROOT/scripts/prepare-bootscreen.sh"
python3 "$ROOT/scripts/prepare-neoemblem.py" "$ROOT/assets/neoemblem-source.txt" "$ROOT/assets/neoemblem.txt"
make -C "$ROOT/src/br8-panel" emblem.h

if [[ "$NATIVE_BUILD" -eq 1 ]]; then
    build_binaries_on_host
fi

if [[ "$USE_ALARM" -eq 0 && ! -f "$BOOTSTRAP" ]]; then
    log "Downloading Arch bootstrap ($ARCH)..."
    curl -fsSL -o "$BOOTSTRAP" \
        "https://geo.mirror.pkgbuild.com/iso/latest/archlinux-bootstrap-${ARCH}.tar.zst"
fi

log "Creating disk image (${SIZE_MB}MB)..."
mkdir -p "$VM_DIR" "$MNT"
if [[ ! -f "$DISK" ]]; then
    truncate -s "${SIZE_MB}M" "$DISK"
fi

DISK_LABEL="backroot8"
[[ "$ARCH" == "aarch64" ]] && DISK_LABEL="backroot8arm"

if ! sudo blkid "$DISK" | grep -q ext4; then
    log "Formatting whole disk as ext4 (LABEL=$DISK_LABEL)..."
    sudo mkfs.ext4 -F -L "$DISK_LABEL" "$DISK"
fi

log "Mounting root..."
if ! mountpoint -q "$MNT"; then
    sudo mount -o loop "$DISK" "$MNT"
fi
LOOP=$(findmnt -n -o SOURCE "$MNT" | sed 's/p[0-9]*$//')

if [[ ! -f "$MNT/bin/bash" ]]; then
    if [[ "$USE_ALARM" -eq 1 ]]; then
        bootstrap_alarm_root
    else
        log "Extracting Arch bootstrap..."
        sudo tar -xf "$BOOTSTRAP" -C "$MNT" --strip-components=1
    fi
fi

if [[ "$ARCH" == "aarch64" && "$HOST_ARCH" == "x86_64" ]]; then
    setup_aarch64_chroot
fi

log "Configuring pacman mirrors..."
if [[ "$USE_ALARM" -eq 1 ]]; then
    write_alarm_pacman_conf
    sudo cp -f "$PACMAN_ALARM_CONF" "$MNT/etc/pacman.conf"
    sudo sed -i 's/^Architecture = auto/Architecture = aarch64/' "$MNT/etc/pacman.conf" 2>/dev/null || true
else
    sudo sed -i 's/^#Server/Server/' "$MNT/etc/pacman.d/mirrorlist" 2>/dev/null || true
fi

log "Chroot setup..."
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

sudo sed -i 's/^#DisableSandbox.*/DisableSandbox/' "$MNT/etc/pacman.conf" 2>/dev/null || true
grep -q '^DisableSandbox' "$MNT/etc/pacman.conf" || \
    sudo sed -i '/^\[options\]/a DisableSandbox' "$MNT/etc/pacman.conf"

sudo arch-chroot "$MNT" env XORG_DRV="$XORG_DRV" KERNEL_PKG="$KERNEL_PKG" VMLINUZ="$VMLINUZ" INITRD_IMG="$INITRD_IMG" USE_ALARM="$USE_ALARM" /bin/bash -eux <<'CHROOT'
pacman-key --init
if [[ "$USE_ALARM" == 1 ]]; then
    pacman-key --populate archlinuxarm
fi
pacman-key --populate archlinux
pacman -Sy --noconfirm

pacman -S --noconfirm --needed \
    "$KERNEL_PKG" linux-firmware \
    base base-devel python \
    xorg-server xorg-xinit xorg-xrandr "$XORG_DRV" \
    xterm dolphin feh fbida nettle xorg-fonts-misc libxft ttf-dejavu x11vnc unzip librsvg \
    poppler-glib cairo zenity \
    systemd-sysvcompat \
    sudo networkmanager \
    mkinitcpio grub efibootmgr \
    dhcpcd openssh

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

systemctl enable NetworkManager
systemctl enable sshd
systemctl enable backroot8-desktop.service 2>/dev/null || true
mkdir -p /etc/systemd/system/getty@tty1.service.d
cat > /etc/systemd/system/getty@tty1.service.d/autologin.conf <<'AUTO'
[Service]
ExecStart=
ExecStart=-/usr/bin/agetty --autologin root --noclear %I $TERM
AUTO

sed -i 's/^HOOKS=.*/HOOKS=(base udev modconf block filesystems fsck)/' /etc/mkinitcpio.conf
mkinitcpio -P

mkdir -p /boot/grub
cat > /boot/grub/grub.cfg <<'GRUB'
set default=0
set timeout=2
menuentry "Backroot 8" {
    set root=(hd0)
    linux /boot/$VMLINUZ root=LABEL=backroot8 rw quiet
    initrd /boot/$INITRD_IMG
}
GRUB
pacman -Syu --noconfirm
CHROOT

if [[ "$NATIVE_BUILD" -eq 0 ]]; then
    build_binaries_in_chroot
fi

install_backroot_overlay

log "Installing Segoe UI font (Microsoft Segoe UI Variable)..."
SEGOE_TMP="$(mktemp -d)"
curl -fsSL -o "$SEGOE_TMP/segoe-ui-variable.zip" "https://aka.ms/SegoeUIVariable"
unzip -q "$SEGOE_TMP/segoe-ui-variable.zip" -d "$SEGOE_TMP/extract"
sudo mkdir -p "$MNT/usr/share/fonts/segoe-ui" "$MNT/usr/share/licenses/segoe-ui-variable"
sudo install -Dm644 "$SEGOE_TMP/extract"/Segoe*.ttf "$MNT/usr/share/fonts/segoe-ui/"
sudo install -Dm644 "$SEGOE_TMP/extract/EULA.txt" "$MNT/usr/share/licenses/segoe-ui-variable/LICENSE"
sudo arch-chroot "$MNT" fc-cache -f
rm -rf "$SEGOE_TMP"

sudo arch-chroot "$MNT" systemctl enable backroot8-splash.service backroot8-desktop.service sshd 2>/dev/null || true

sudo mkdir -p "$MNT/root"
cat <<'EOF' | sudo tee "$MNT/root/.xinitrc" >/dev/null
#!/bin/sh
exec /etc/X11/xinit/xinitrc
EOF
sudo chmod +x "$MNT/root/.xinitrc"

cat <<'EOF' | sudo tee "$MNT/root/.bash_profile" >/dev/null
[[ -z $DISPLAY && $XDG_VTNR -eq 1 ]] && exec startx
EOF

# aarch64 image uses a distinct ext4 label for multi-arch VM hosts
if [[ "$ARCH" == "aarch64" ]]; then
    sudo sed -i 's/LABEL=backroot8/LABEL=backroot8arm/' "$MNT/boot/grub/grub.cfg"
fi

LOOP_DEV="${LOOP:-$(losetup -j "$DISK" 2>/dev/null | awk -F: '{print $1}' | head -1)}"
if [[ -n "$LOOP_DEV" && -b "$LOOP_DEV" ]]; then
    if [[ "$ARCH" == "x86_64" ]]; then
        log "Installing GRUB ($GRUB_TARGET) to $LOOP_DEV..."
        sudo grub-install --target="$GRUB_TARGET" --boot-directory="$MNT/boot" --recheck "$LOOP_DEV"
    else
        log "Skipping BIOS GRUB on aarch64 (QEMU boots via -kernel; use UEFI image for hardware)"
    fi
fi

cleanup_mounts
trap - EXIT

BUILD_USER="${SUDO_USER:-$USER}"
if [[ -n "$BUILD_USER" && "$BUILD_USER" != "root" ]]; then
    sudo chown "$BUILD_USER:$BUILD_USER" "$DISK" 2>/dev/null || true
fi

log "Rootfs build complete: $DISK"
