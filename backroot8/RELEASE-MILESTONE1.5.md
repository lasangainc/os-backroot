# Backroot 8 Milestone 1.5

Bootable hybrid live ISO for BIOS and UEFI systems. Supersedes [Milestone 1](RELEASE-MILESTONE1.md) for new installs and QEMU testing.

## Download

**[GitHub Release v8-milestone1.5](https://github.com/lasangainc/os-backroot/releases/tag/v8-milestone1.5)** — asset `backroot8-milestone1.5.iso`.

## Build

On Ubuntu/Debian (host deps once):

```bash
cd backroot8
sudo ./scripts/install-iso-build-deps.sh
export BACKROOT8_VERSION=8-milestone1.5
sudo ./scripts/build-root.sh
sudo ./scripts/build-iso.sh
```

Writes `vm/backroot8-live.iso`. To build, verify boot, and publish the GitHub release asset:

```bash
sudo ./scripts/publish-milestone1.5-release.sh
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

Replace `/dev/sdX` with the whole USB device (not a partition). For the release download, use `backroot8-milestone1.5.iso`.

## Boot on hardware

1. Boot from USB in the firmware menu.
2. Select **Backroot 8 Live** in GRUB.
3. Desktop starts on tty1 (root autologin on live image).

Credentials: `root` / `backroot8`

## Technical notes

- Same live architecture as Milestone 1: squashfs root on ISO, tmpfs overlay, `backroot8_iso` + `backroot8_root` hooks.
- Plymouth + `backroot8_splash` / `backroot8-fb-splash` for boot branding; Xorg **modesetting** for QEMU `-vga std`.
- VM disk images are not used; development and release both boot this ISO.
