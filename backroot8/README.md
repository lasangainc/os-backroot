# Backroot 8

A minimal desktop environment built from scratch on **Arch Linux** with the precompiled **`linux`** kernel package.

## Features

- Custom window manager (`br8-wm`): draggable windows, minimize, maximize, close
- Close button: elongated **X** inside a **red square** border
- Right-click desktop → **New terminal at root** or **Dolphin file explorer**
- Window titles show the app name, centered in the title bar
- Taskbar shows open apps with icons (click to focus)
- Transparent taskbar (`br8-panel`) — alpha blend only, **no blur**
- Arch precompiled kernel (`linux` package from official repos)

## Quick start (VM)

```bash
# Build disk image (~4GB, requires network)
sudo ./scripts/build-rootfs.sh

# Start VM + open GUI in browser
./scripts/run-vm-gui.sh
```

**Use the GUI in your browser** (not SSH):

**http://localhost:6080/vnc.html?autoconnect=1&resize=scale**

In Cursor: open **Ports**, forward **6080**, then click the forwarded link.

Optional: connect a VNC app to `localhost:5902`. The desktop starts automatically on boot (no login needed).

## Project layout

| Path | Purpose |
|------|---------|
| `src/br8-wm/` | Window manager (X11) |
| `src/br8-panel/` | Taskbar |
| `rootfs-overlay/` | Session and motd |
| `scripts/build-rootfs.sh` | Arch bootstrap + package install |
| `scripts/run-vm.sh` | QEMU headless + VNC |
| `vm/backroot8.img` | Bootable disk image |

## Rebuild desktop binaries only

```bash
make -C src/br8-wm && make -C src/br8-panel
sudo cp src/br8-wm/br8-wm vm/mnt/usr/local/bin/   # when mounted
```
