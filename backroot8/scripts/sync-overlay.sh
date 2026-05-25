#!/usr/bin/env bash
# Install compiled Backroot binaries and rootfs-overlay into a rootfs tree.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="${1:-}"

if [[ -z "$DEST" || ! -d "$DEST" ]]; then
    echo "Usage: $0 /path/to/rootfs" >&2
    exit 1
fi

log() { echo "[sync-overlay] $*"; }

run_make() {
    if [[ -n "${SUDO_USER:-}" ]] && [[ "$SUDO_USER" != "root" ]]; then
        sudo -u "$SUDO_USER" "$@"
    else
        "$@"
    fi
}

log "Building desktop binaries..."
run_make "$ROOT/scripts/prepare-bootscreen.sh"
run_make "$ROOT/scripts/prepare-install-banner.sh"
run_make python3 "$ROOT/scripts/prepare-neoemblem.py" \
    "$ROOT/assets/neoemblem-source.txt" "$ROOT/assets/neoemblem.txt"
run_make make -C "$ROOT/src/br8-panel" emblem.h
run_make "$ROOT/scripts/prepare-openbox-theme.sh"
run_make make -C "$ROOT/src/br8-wm" clean br8-wm
run_make make -C "$ROOT/src/br8-panel" clean br8-panel
run_make make -C "$ROOT/src/br8-metro-helper" clean br8-metro-helper
run_make make -C "$ROOT/src/br8-start" clean br8-start
run_make make -C "$ROOT/src/br8-settings" clean br8-settings
run_make make -C "$ROOT/src/power-pdf" clean powerpdf
run_make make -C "$ROOT/src/br8-install" clean br8-install
run_make make -C "$ROOT/src/br8-oobe" clean br8-oobe

install -Dm755 "$ROOT/src/br8-wm/br8-wm" "$DEST/usr/local/bin/br8-wm"
install -Dm755 "$ROOT/src/br8-metro-helper/br8-metro-helper" "$DEST/usr/local/bin/br8-metro-helper"
install -Dm755 "$ROOT/src/br8-panel/br8-panel" "$DEST/usr/local/bin/br8-panel"
install -Dm644 "$ROOT/rootfs-overlay/etc/xdg/openbox/rc.xml" "$DEST/etc/xdg/openbox/rc.xml"
install -Dm644 "$ROOT/rootfs-overlay/etc/xdg/openbox/menu.xml" "$DEST/etc/xdg/openbox/menu.xml"
mkdir -p "$DEST/usr/share/themes/Backroot8-Sharp/openbox-3"
cp -a "$ROOT/rootfs-overlay/usr/share/themes/Backroot8-Sharp/openbox-3/." \
    "$DEST/usr/share/themes/Backroot8-Sharp/openbox-3/"
install -Dm755 "$ROOT/src/br8-start/br8-start" "$DEST/usr/local/bin/br8-start"
install -Dm755 "$ROOT/src/br8-settings/br8-settings" "$DEST/usr/local/bin/br8-settings"
install -Dm755 "$ROOT/src/power-pdf/powerpdf" "$DEST/usr/local/bin/powerpdf"
install -Dm755 "$ROOT/src/br8-install/br8-install" "$DEST/usr/local/bin/br8-install"
install -Dm755 "$ROOT/src/br8-oobe/br8-oobe" "$DEST/usr/local/bin/br8-oobe"
install -Dm755 "$ROOT/rootfs-overlay/usr/lib/backroot8/is-live-boot.sh" \
    "$DEST/usr/lib/backroot8/is-live-boot.sh"
install -Dm755 "$ROOT/rootfs-overlay/usr/lib/backroot8/br8-install-to-disk.sh" \
    "$DEST/usr/lib/backroot8/br8-install-to-disk.sh"
install -Dm755 "$ROOT/rootfs-overlay/usr/lib/backroot8/br8-oobe-setup.sh" \
    "$DEST/usr/lib/backroot8/br8-oobe-setup.sh"
install -Dm755 "$ROOT/rootfs-overlay/usr/lib/backroot8/br8-oobe-finish-login.sh" \
    "$DEST/usr/lib/backroot8/br8-oobe-finish-login.sh"
install -Dm755 "$ROOT/rootfs-overlay/usr/lib/backroot8/br8-desktop-session.sh" \
    "$DEST/usr/lib/backroot8/br8-desktop-session.sh"
install -Dm755 "$ROOT/rootfs-overlay/usr/lib/backroot8/br8-list-install-disks.sh" \
    "$DEST/usr/lib/backroot8/br8-list-install-disks.sh"

install -Dm644 "$ROOT/rootfs-overlay/usr/share/backroot/README" \
    "$DEST/usr/share/backroot/README"
install -Dm644 "$ROOT/rootfs-overlay/usr/share/applications/br8-settings.desktop" \
    "$DEST/usr/share/applications/br8-settings.desktop"
install -Dm644 "$ROOT/rootfs-overlay/usr/share/applications/powerpdf.desktop" \
    "$DEST/usr/share/applications/powerpdf.desktop"
install -Dm755 "$ROOT/rootfs-overlay/etc/X11/xinit/xinitrc" "$DEST/etc/X11/xinit/xinitrc"
install -Dm644 "$ROOT/rootfs-overlay/usr/share/backgrounds/backroot8.jpg" \
    "$DEST/usr/share/backgrounds/backroot8.jpg"
install -Dm644 "$ROOT/rootfs-overlay/usr/share/backroot8/bootscreen.png" \
    "$DEST/usr/share/backroot8/bootscreen.png"
install -Dm755 "$ROOT/rootfs-overlay/usr/lib/backroot8/br8-bootscreen.sh" \
    "$DEST/usr/lib/backroot8/br8-bootscreen.sh"
install -Dm644 "$ROOT/rootfs-overlay/usr/share/backroot8/install-banner.png" \
    "$DEST/usr/share/backroot8/install-banner.png"
install -Dm644 "$ROOT/rootfs-overlay/usr/share/backroot8/default-user.png" \
    "$DEST/usr/share/backroot8/default-user.png"
if [ -d "$ROOT/rootfs-overlay/usr/share/backroot8/oobe-wallpapers" ]; then
    mkdir -p "$DEST/usr/share/backroot8/oobe-wallpapers"
    for _wp in "$ROOT/rootfs-overlay/usr/share/backroot8/oobe-wallpapers/"*.jpg; do
        [ -f "$_wp" ] || continue
        install -Dm644 "$_wp" "$DEST/usr/share/backroot8/oobe-wallpapers/$(basename "$_wp")"
    done
fi
install -Dm644 "$ROOT/rootfs-overlay/etc/tmpfiles.d/br8-oobe.conf" \
    "$DEST/etc/tmpfiles.d/br8-oobe.conf"
install -Dm755 "$ROOT/rootfs-overlay/usr/lib/backroot8/br8-plymouth-quit.sh" \
    "$DEST/usr/lib/backroot8/br8-plymouth-quit.sh"
install -Dm755 "$ROOT/rootfs-overlay/usr/share/backroot8/show-fb-splash.sh" \
    "$DEST/usr/share/backroot8/show-fb-splash.sh"
install -Dm644 "$ROOT/rootfs-overlay/etc/systemd/system/backroot8-fb-splash.service" \
    "$DEST/etc/systemd/system/backroot8-fb-splash.service"
install -Dm755 "$ROOT/rootfs-overlay/usr/share/backroot8/br8-panel-launcher.sh" \
    "$DEST/usr/share/backroot8/br8-panel-launcher.sh"
install -Dm644 "$ROOT/rootfs-overlay/etc/profile.d/backroot8.sh" "$DEST/etc/profile.d/backroot8.sh"
install -Dm644 "$ROOT/rootfs-overlay/etc/profile.d/backroot8-neofetch.sh" \
    "$DEST/etc/profile.d/backroot8-neofetch.sh"
install -Dm644 "$ROOT/rootfs-overlay/etc/os-release" "$DEST/etc/os-release"
install -Dm644 "$ROOT/rootfs-overlay/etc/os-release" "$DEST/usr/lib/os-release"
install -Dm644 "$ROOT/assets/neoemblem.txt" "$DEST/usr/share/backroot8/neoemblem.txt"
install -Dm644 "$ROOT/rootfs-overlay/etc/neofetch/config.conf" "$DEST/etc/neofetch/config.conf"
mkdir -p "$DEST/root/.config/neofetch"
install -Dm644 "$ROOT/rootfs-overlay/etc/neofetch/config.conf" \
    "$DEST/root/.config/neofetch/config.conf"
install -Dm755 "$ROOT/rootfs-overlay/usr/local/bin/neofetch" "$DEST/usr/local/bin/neofetch"
install -Dm644 "$ROOT/rootfs-overlay/etc/motd" "$DEST/etc/motd"
install -Dm644 "$ROOT/rootfs-overlay/etc/systemd/system/backroot8-desktop.service" \
    "$DEST/etc/systemd/system/backroot8-desktop.service"
install -Dm644 "$ROOT/rootfs-overlay/etc/plymouth/plymouthd.conf" \
    "$DEST/etc/plymouth/plymouthd.conf"
install -Dm644 "$ROOT/rootfs-overlay/usr/share/plymouth/themes/backroot8/backroot8.plymouth" \
    "$DEST/usr/share/plymouth/themes/backroot8/backroot8.plymouth"
install -Dm644 "$ROOT/rootfs-overlay/usr/share/plymouth/themes/backroot8/backroot8.script" \
    "$DEST/usr/share/plymouth/themes/backroot8/backroot8.script"
install -Dm644 "$ROOT/rootfs-overlay/usr/share/plymouth/themes/backroot8/bootscreen.png" \
    "$DEST/usr/share/plymouth/themes/backroot8/bootscreen.png"
install -Dm644 "$ROOT/rootfs-overlay/usr/share/plymouth/themes/backroot8/emblem.png" \
    "$DEST/usr/share/plymouth/themes/backroot8/emblem.png"
install -Dm644 "$ROOT/rootfs-overlay/etc/systemd/system/backroot8-live-cow.service" \
    "$DEST/etc/systemd/system/backroot8-live-cow.service"
install -Dm755 "$ROOT/rootfs-overlay/usr/lib/backroot8/live-cow-setup.sh" \
    "$DEST/usr/lib/backroot8/live-cow-setup.sh"
install -Dm644 "$ROOT/rootfs-overlay/etc/X11/xorg.conf.d/10-vesa.conf" \
    "$DEST/etc/X11/xorg.conf.d/10-vesa.conf"
install -Dm644 "$ROOT/rootfs-overlay/etc/fonts/conf.d/99-segoe-ui.conf" \
    "$DEST/etc/fonts/conf.d/99-segoe-ui.conf"
install -Dm755 "$ROOT/rootfs-overlay/etc/initcpio/install/backroot8_iso" \
    "$DEST/etc/initcpio/install/backroot8_iso"
install -Dm755 "$ROOT/rootfs-overlay/etc/initcpio/hooks/backroot8_iso" \
    "$DEST/etc/initcpio/hooks/backroot8_iso"
install -Dm755 "$ROOT/rootfs-overlay/etc/initcpio/install/backroot8_root" \
    "$DEST/etc/initcpio/install/backroot8_root"
install -Dm755 "$ROOT/rootfs-overlay/etc/initcpio/hooks/backroot8_root" \
    "$DEST/etc/initcpio/hooks/backroot8_root"
install -Dm755 "$ROOT/rootfs-overlay/etc/initcpio/hooks/br8_boot_util" \
    "$DEST/etc/initcpio/hooks/br8_boot_util"
install -Dm755 "$ROOT/rootfs-overlay/etc/initcpio/install/backroot8_splash" \
    "$DEST/etc/initcpio/install/backroot8_splash"
install -Dm755 "$ROOT/rootfs-overlay/etc/initcpio/hooks/backroot8_splash" \
    "$DEST/etc/initcpio/hooks/backroot8_splash"

log "Overlay sync complete: $DEST"
