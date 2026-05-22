# Backroot 8 Milestone 1

Bootable hybrid ISO for BIOS and UEFI systems.

## Download

See GitHub Releases: **backroot8-milestone1.iso** (~1.4 GB).

## Write to USB

```bash
sudo dd if=backroot8-milestone1.iso of=/dev/sdX bs=4M status=progress conv=fsync
```

Replace `/dev/sdX` with your USB device (not a partition).

## Boot

1. Boot from USB or DVD in firmware boot menu.
2. Select **Backroot 8 Milestone 1** in GRUB.
3. Desktop starts automatically (root autologin on tty1).

Default credentials: `root` / `backroot8`

## Build from source

```bash
sudo ./scripts/build-rootfs.sh
sudo ./scripts/build-iso.sh
./scripts/verify-iso-boot.sh
```

## Technical notes

- Kernel and initramfs load from ISO9660; root is an ext4 image inside `backroot8-root.squashfs`.
- Kernel parameter `backroot8iso` triggers the `backroot8_iso` mkinitcpio hook.
- Disk image (`vm/backroot8.img`) remains available for QEMU development via `run-vm-gui.sh`.
