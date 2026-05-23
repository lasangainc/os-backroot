# Backroot 8 Milestone 1

Bootable hybrid live ISO for BIOS and UEFI systems.

## Download

**[GitHub Release v8-milestone1](https://github.com/lasangainc/os-backroot/releases/tag/v8-milestone1)** — asset `backroot8-milestone1.iso` (~1.1 GB).

## Build

On Ubuntu/Debian (host deps once):

```bash
cd backroot8
sudo ./scripts/install-iso-build-deps.sh
sudo ./scripts/build-root.sh
sudo ./scripts/build-iso.sh
```

Writes `vm/backroot8-live.iso`. To refresh the GitHub release asset after a fix:

```bash
sudo ./scripts/publish-milestone1-release.sh
```

Iterate on the desktop, then rebuild:

```bash
# After editing src/ or rootfs-overlay/
sudo ./scripts/build-root.sh
sudo ./scripts/build-iso.sh
```

Package list: [`packages.backroot8.txt`](packages.backroot8.txt)

## Test in QEMU

```bash
./scripts/verify-iso-boot.sh
./scripts/run-vm-gui.sh
```

**http://localhost:6080/vnc.html?autoconnect=1&resize=scale**

## Write to USB

```bash
sudo dd if=vm/backroot8-live.iso of=/dev/sdX bs=4M status=progress conv=fsync
```

Replace `/dev/sdX` with the whole USB device (not a partition).

## Boot on hardware

1. Boot from USB in the firmware menu.
2. Select **Backroot 8 Live** in GRUB.
3. Desktop starts on tty1 (root autologin).

Credentials: `root` / `backroot8`

## Technical notes

- Kernel and initramfs load from the ISO; root is a **squashfs** tree on the ISO with a **tmpfs overlay** for a writable live system (`backroot8_iso` + `backroot8_root` mkinitcpio hooks).
- Kernel parameter `backroot8iso` selects the live boot path ([`rootfs-overlay/etc/initcpio/`](rootfs-overlay/etc/initcpio/)).
- QEMU / emulated VGA: Xorg uses the **modesetting** driver (vesa conflicts with the kernel framebuffer).
- VM disk images (`backroot8.img`) are no longer used; development boots the same ISO as release.
