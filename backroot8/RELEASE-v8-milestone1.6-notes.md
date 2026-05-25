## Backroot 8 Milestone 1.6

Bootable hybrid live ISO for x86_64 PCs (BIOS + UEFI). Fixes critical regressions in Milestone 1.5.

### Download

- **backroot8-milestone1.6.iso** — write to USB with `dd` or boot from optical media / QEMU

### Supersedes Milestone 1.5 (DEFECTIVE)

Milestone 1.5 shipped broken first-run setup (OOBE), installer checkmarks, and login session behavior. **Do not use 1.5.** This release is the supported path from 1.5.

### Fixes

- Installer: completed steps show proper checkmarks (not missing-character boxes)
- OOBE intro: “Personalize” and “Sign in” labels fit on screen
- OOBE account: Finish creates users with passwords; inline errors when validation fails
- OOBE loading: “We are setting up your computer” dismisses when the taskbar is ready
- Login: no surprise `xterm` window on every session start

### Carried forward from 1.5

- Plymouth splash, Start menu, Settings, live installer, OOBE flow, PowerPDF, Segoe UI chrome
- Live squashfs + tmpfs overlay (`backroot8iso` hooks)

### Quick start

```bash
sudo dd if=backroot8-milestone1.6.iso of=/dev/sdX bs=4M status=progress conv=fsync
```

Build from source: `backroot8/RELEASE-MILESTONE1.6.md`

Test in browser (QEMU + noVNC): http://localhost:6080/vnc.html?autoconnect=1&resize=scale
