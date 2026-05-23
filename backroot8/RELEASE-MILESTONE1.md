# Backroot 8 Milestone 1

Bootable hybrid live ISO for BIOS and UEFI systems.

## Build

On Ubuntu/Debian (host deps once):

```bash
sudo ./scripts/install-iso-build-deps.sh
sudo ./scripts/build-iso.sh
```

`build-iso.sh` builds the Arch root under `vm/rootfs/` (first run) and writes `vm/backroot8-live.iso`.

Iterate on the desktop, then rebuild:

```bash
# After editing src/ or rootfs-overlay/
sudo ./scripts/build-root.sh    # refresh rootfs only
sudo ./scripts/build-iso.sh     # new ISO
```

Package list: [`packages.backroot8.txt`](packages.backroot8.txt)

## Test in QEMU

```bash
./scripts/verify-iso-boot.sh    # headless boot check (serial log)
./scripts/run-vm-gui.sh         # browser GUI via noVNC
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

- Kernel and initramfs load from the ISO; root is an ext4 image inside `backroot8-root.squashfs`.
- Kernel parameter `backroot8iso` triggers the `backroot8_iso` mkinitcpio hook ([`rootfs-overlay/etc/initcpio/`](rootfs-overlay/etc/initcpio/)).
- VM disk images (`backroot8.img`) are no longer used; development boots the same ISO as release.
