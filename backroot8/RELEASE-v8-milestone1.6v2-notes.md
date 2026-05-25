## Backroot 8 Milestone 1.6v2

Bootable hybrid live ISO for x86_64 PCs (BIOS + UEFI). Rebuild of Milestone 1.6 with start-menu and OOBE loading fixes.

### Download

- **backroot8-milestone1.6v2.iso** — write to USB with `dd` or boot from optical media / QEMU

### What's new in 1.6v2 (since 1.6)

- **Start menu** opens quickly: one long-lived `br8-start`, lazy wallpaper load, per-user layout config
- **OOBE loading** shows a live restart countdown (30s) and auto-restarts the desktop if setup stalls
- All **1.6** fixes retained: installer checkmarks, OOBE intro/account, no login xterm, loading handoff

### Do not use

- **[Milestone 1.5](https://github.com/lasangainc/os-backroot/releases/tag/v8-milestone1.5)** — DEFECTIVE
- **1.6** first ISO if you need this build — prefer **1.6v2**

### Quick start

```bash
sudo dd if=backroot8-milestone1.6v2.iso of=/dev/sdX bs=4M status=progress conv=fsync
```

Build from source: `backroot8/RELEASE-MILESTONE1.6.md`

Test in browser (QEMU + noVNC): http://localhost:6080/vnc.html?autoconnect=1&resize=scale
