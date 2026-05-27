## Backroot 8 Milestone 1.5

Bootable hybrid live ISO for x86_64 PCs (BIOS + UEFI). Builds on [Milestone 1](https://github.com/lasangainc/os-backroot/releases/tag/v8-milestone1) with the current `main` desktop stack.

### Download

- **backroot8-milestone1.5.iso** — write to USB with `dd` or boot from optical media / QEMU

### What's new since Milestone 1

- **Plymouth** quiet boot splash (`backroot8` theme) plus framebuffer boot emblem through early boot
- **Start menu** (`br8-start`): slide-in animation, user header, Desktop tile wallpaper preview, app grid on scroll
- **Settings** metro app (`br8-settings`): wallpaper, display, keyboard, mouse, time zone
- **Live USB installer** (`br8-install`) and **first-boot OOBE** (`br8-oobe`) with intro slides, transitions, and account setup
- **Segoe UI** for title bars and taskbar clock
- **Win7/8-style** close button (elongated X in red square at title bar top)
- **PowerPDF** document viewer
- Same live root: squashfs on ISO + tmpfs overlay (`backroot8iso` / mkinitcpio hooks)

### Verification

- QEMU serial boot: live overlay root and `backroot8 login:` or `Started Backroot 8 Desktop`
- Desktop: WM, panel, xterm; Start button; Settings; installer on live boot

### Quick start

```bash
sudo dd if=backroot8-milestone1.5.iso of=/dev/sdX bs=4M status=progress conv=fsync
```

Boot **Backroot 8 Live** in GRUB. Desktop autostarts on tty1.

**Login:** `root` / `backroot8`

Build from source: `backroot8/RELEASE-MILESTONE1.5.md`
