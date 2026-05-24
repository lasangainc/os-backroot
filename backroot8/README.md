# Backroot 8

A minimal desktop environment built from scratch on **Arch Linux** with the precompiled **`linux`** kernel package.

**Milestone 1** ships as a bootable live ISO (`vm/backroot8-live.iso`) for USB, optical media, and QEMU.

## Features

- Custom window manager (`br8-wm`): draggable windows, minimize, maximize, close
- Close button: elongated **X** inside a **red square** border
- Right-click desktop → **New terminal** or **file manager** (pcmanfm)
- Window titles show the app name, centered in the title bar
- Taskbar shows open apps with icons (click to focus)
- Transparent taskbar (`br8-panel`) — alpha blend only, **no blur**
- Plymouth boot splash (`backroot8` theme) from initramfs through desktop load
- Default wallpaper at `/usr/share/backgrounds/backroot8.jpg` (via `feh`)
- PowerPDF viewer and Settings metro app (wallpaper, display, keyboard, mouse, time zone)
- Arch precompiled kernel (`linux` package from official repos)

## Quick start

```bash
# Host tools (Ubuntu/Debian, once)
sudo ./scripts/install-iso-build-deps.sh

# Build live ISO (network + sudo; first run downloads Arch bootstrap)
sudo ./scripts/build-iso.sh

# Boot ISO in QEMU + browser GUI
./scripts/run-vm-gui.sh
```

**Browser (Cursor: forward port 6080):**

**http://localhost:6080/vnc.html?autoconnect=1&resize=scale**

Credentials in the live system: `root` / `backroot8`

See [RELEASE-MILESTONE1.md](RELEASE-MILESTONE1.md) for USB writing and iteration workflow.

## Iterate and rebuild

| Change | Command |
|--------|---------|
| WM, panel, apps (`src/`) | `sudo ./scripts/build-root.sh` then `sudo ./scripts/build-iso.sh` |
| Session, overlay (`rootfs-overlay/`) | same |
| New Arch package | edit `packages.backroot8.txt`, then same |

`build-rootfs.sh` only prints a deprecation notice and runs `build-root.sh` (use `build-iso.sh` for the bootable image).

## Project layout

| Path | Purpose |
|------|---------|
| `src/br8-wm/` | Window manager (X11) |
| `src/br8-panel/` | Taskbar |
| `src/br8-start/` | Start menu overlay |
| `src/power-pdf/` | PowerPDF viewer |
| `rootfs-overlay/` | Session, initcpio ISO hook, systemd units |
| `packages.backroot8.txt` | Shared pacman package list |
| `scripts/build-iso.sh` | Build `vm/backroot8-live.iso` |
| `scripts/build-root.sh` | Populate `vm/rootfs/` |
| `scripts/run-vm-gui.sh` | QEMU (ISO) + noVNC |
| `vm/backroot8-live.iso` | Bootable live image (gitignored) |

## Rebuild desktop binaries only (on host)

```bash
make -C src/br8-wm && make -C src/br8-panel
# Then refresh rootfs/ISO:
sudo ./scripts/build-root.sh && sudo ./scripts/build-iso.sh
```
