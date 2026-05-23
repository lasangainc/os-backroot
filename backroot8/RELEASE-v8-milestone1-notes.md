## Backroot 8 Milestone 1 (re-release)

Bootable hybrid live ISO for x86_64 PCs (BIOS + UEFI). Replaces the initial broken asset with a verified build.

### Download

- **backroot8-milestone1.iso** (~1.1 GB) — write to USB with `dd` or boot from optical media

### What changed

- Live root uses **squashfs + tmpfs overlay** (writable system without copying the whole root to RAM)
- Initramfs includes util-linux `mount` for overlay (fixes blank boot / mount failure)
- Xorg **modesetting** driver for QEMU and typical framebuffer setups (fixes desktop not starting)

### Verification

- QEMU serial boot: `backroot8_root: live root on overlay` and `backroot8 login:`
- Desktop: `backroot8-desktop` (WM, panel, xterm)

### Quick start

```bash
sudo dd if=backroot8-milestone1.iso of=/dev/sdX bs=4M status=progress conv=fsync
```

Boot **Backroot 8 Live** in GRUB. Desktop autostarts on tty1.

**Login:** `root` / `backroot8`

Build from source: `backroot8/RELEASE-MILESTONE1.md`
