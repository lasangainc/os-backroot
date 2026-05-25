## Backroot 8 Milestone 1.6v3

Bootable hybrid live ISO for x86_64 PCs (BIOS + UEFI). Latest 1.6 line with OOBE loading pipeline fixes and live OOBE debug tooling.

### Download

- **backroot8-milestone1.6v3.iso** — write to USB with `dd` or boot from optical media / QEMU

### What's new in 1.6v3

- **OOBE loading** runs create-account, sign-in, and desktop restart in the background (no restart countdown message)
- **`trigger-oobe-debug`** — on live ISO/USB, run as root to arm and launch the full OOBE wizard for testing

### Live OOBE debug

```bash
sudo trigger-oobe-debug
```

Requires live boot (`backroot8iso` on kernel cmdline). Restarts the desktop and starts OOBE.

### Prefer this over

- Milestone 1.5 (DEFECTIVE)
- 1.6 / 1.6v2 if you need the latest OOBE behavior

### Quick start

```bash
sudo dd if=backroot8-milestone1.6v3.iso of=/dev/sdX bs=4M status=progress conv=fsync
```

Build: `backroot8/RELEASE-MILESTONE1.6.md`

Test: http://localhost:6080/vnc.html?autoconnect=1&resize=scale
