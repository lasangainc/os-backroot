# AGENTS.md — Backroot 8

Guide for AI agents working on **os-backroot** / **Backroot 8**: a minimal X11 desktop on Arch Linux, developed and tested in QEMU.

## Project summary

| Item | Detail |
|------|--------|
| **Goal** | Custom DE from scratch on Arch with precompiled `linux` kernel |
| **WM** | `br8-wm` — C, Xlib only |
| **Panel** | `br8-panel` — C, Xlib only |
| **Session** | `startx` → `xinitrc` → panel + xterm + `exec br8-wm` |
| **Default branch** | `main` |
| **Feature work** | `cursor/<name>-4238` (cloud agent convention) |

All product code lives under **`backroot8/`**. Repo root is a thin pointer (`README.md` → `backroot8/README.md`).

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│  QEMU (kernel+initrd direct boot, disk = rootfs)        │
│  ┌───────────────────────────────────────────────────┐  │
│  │ Xorg :0                                           │  │
│  │  root                                             │  │
│  │   ├── br8-panel (override_redirect, bottom)       │  │
│  │   ├── br8-wm manages frames per app window        │  │
│  │   │     frame → title, buttons, reparented client │  │
│  │   └── xterm / other clients                       │  │
│  └───────────────────────────────────────────────────┘  │
│  systemd: backroot8-desktop.service → startx            │
└─────────────────────────────────────────────────────────┘
         ▲ VNC :5902          ▲ noVNC http://localhost:6080
```

### WM ↔ panel protocol (private atoms)

| Atom | Set by | Purpose |
|------|--------|---------|
| `_BR8_FRAME` | WM on frame | Mark WM-decorated frame (CARDINAL `1`) |
| `_BR8_CLIENT` | WM on frame | Client window ID (WINDOW) |
| `_BR8_PANEL_REV` | WM on root | Increment to refresh taskbar |
| `_BR8_ACTIVATE` | Panel on root | WINDOW = frame to restore (delete after read) |
| `_NET_CLIENT_LIST` | WM on root | EWMH client list |

**Taskbar restore:** Panel sets `_BR8_ACTIVATE` on root **and** `XMapWindow`/`XMapSubwindows` on the frame. WM handles `PropertyNotify` on root. Do **not** rely on `ClientMessage` to root alone — it often does not reach the WM.

**Minimize / restore:** Never `add_client()` on an existing frame or chrome window — that caused stacked title bars. `is_our_chrome()` must use valid `XQueryTree` children pointers (never `NULL` for children).

## Directory map

```
backroot8/
├── src/br8-wm/br8-wm.c      # Window manager
├── src/br8-panel/br8-panel.c  # Taskbar
├── rootfs-overlay/            # Copied into disk at build time
│   ├── etc/X11/xinit/xinitrc
│   └── etc/systemd/system/backroot8-desktop.service
├── scripts/
│   ├── build-rootfs.sh        # Arch bootstrap → vm/backroot8.img
│   ├── run-vm.sh              # QEMU + VNC
│   └── run-vm-gui.sh          # run-vm + websockify/noVNC :6080
└── vm/                        # Gitignored artifacts (see .gitignore)
    ├── backroot8.img          # ~4GB ext4 whole-disk image
    ├── vmlinuz-linux          # Extracted for QEMU -kernel boot
    └── initramfs-linux.img
```

## Common tasks

### Build desktop binaries (host)

```bash
make -C backroot8/src/br8-wm
make -C backroot8/src/br8-panel
```

Requires: `gcc`, `libx11-dev`, `pkg-config`.

### Full disk image (first time / package changes)

```bash
cd backroot8
sudo ./scripts/build-rootfs.sh
```

- Needs network, `sudo`, ~4GB disk, Arch bootstrap tarball (auto-downloaded).
- Chroot uses `DisableSandbox` in `pacman.conf` (Landlock fails on some hosts).
- Whole-disk ext4 (no GRUB embed); QEMU boots via **`-kernel` / `-initrd`** in `run-vm.sh`.
- Post-build: `chown` disk image to build user for QEMU.

### Run VM for manual testing

```bash
./backroot8/scripts/run-vm-gui.sh
```

Browser: `http://localhost:6080/vnc.html?autoconnect=1&resize=scale`  
SSH (debug): `ssh -p 2222 root@localhost` password `backroot8`

### Hot-deploy binaries into running VM

```bash
# WM/panel must not be running during scp, or copy fails
sshpass -p backroot8 ssh -p 2222 root@127.0.0.1 'systemctl stop backroot8-desktop'
scp backroot8/src/br8-wm/br8-wm backroot8/src/br8-panel/br8-panel root@127.0.0.1:/usr/local/bin/
ssh -p 2222 root@127.0.0.1 'systemctl start backroot8-desktop'
```

### Restart VM cleanly

```bash
pkill -f 'qemu-system-x86_64.*-name backroot8' || true
rm -f backroot8/vm/qemu.pid backroot8/vm/websockify.pid
./backroot8/scripts/run-vm-gui.sh
```

Check zombies: `ps` STAT `Z` on qemu PID — remove stale `qemu.pid` before restart (`run-vm.sh` handles this).

## UI behavior (do not regress)

1. **Title bar:** App name from `_NET_WM_NAME` / `WM_NAME` / `WM_CLASS`, **centered**; buttons on the right.
2. **Close:** Elongated X inside **red square** border (`draw_close_button`).
3. **Right-click root:** Single item — **New terminal at root** (spawns `xterm` at `/`, `DISPLAY=:0`).
4. **Taskbar:** Slightly transparent (alpha blend), **not blurred**; app badges (class letter / color); click restores minimized windows.
5. **Minimize:** Unmaps frame; taskbar click must show window again without duplicate decorations.

## Known pitfalls

| Issue | Cause | Fix |
|-------|--------|-----|
| Taskbar missing | `br8-panel` crashed on `X_PutImage` for `_NET_WM_ICON` | Use `icon_fallback()`; `XSetErrorHandler(NULL)` |
| Stacked title bars | `MapRequest` on frame re-wrapped by WM | `is_our_chrome()` + skip `add_client` |
| WM core dump at boot | `XQueryTree(..., NULL, NULL)` for children | Always pass `&children`, `&nch`, then `XFree` |
| Taskbar click no-op | `ClientMessage` to root ignored | `_BR8_ACTIVATE` property + direct `XMapWindow` |
| QEMU permission denied | `backroot8.img` owned by root | `chown` after build; see `build-rootfs.sh` |
| `xfce4-terminal` broken | libhogweed SONAME skew in image | Prefer `xterm`; run `pacman -Syu` in guest if needed |
| Panel not in git | Runtime files under `vm/` | Listed in `backroot8/.gitignore` |

## Code conventions

- **C / X11:** Minimal dependencies (Xlib only). No compositor, no toolkit.
- **Scope:** Smallest correct diff; match existing style in `br8-wm.c` / `br8-panel.c`.
- **Comments:** Only for non-obvious protocol or X11 quirks.
- **Do not commit:** `vm/*.img`, bootstrap tarball, `qemu.pid`, `websockify.log`, compiled binaries.

## systemd session

`backroot8-desktop.service` runs `startx` on tty1. `xinitrc`:

1. Panel loop: `( while true; do br8-panel; sleep 1; done ) &`
2. Optional default `xterm`
3. `exec br8-wm` (keeps session alive)

`getty@tty1` is disabled when desktop service is enabled (avoid tty/session fight).

## PR / git (cloud agents)

- Branch prefix: `cursor/`
- Push: `git push -u origin <branch>`
- Base: `main`
- Open draft PR when user-facing changes land; reference GUI test URL (port 6080).

## Verification checklist

After WM/panel changes:

1. `make` both targets without warnings (fix new warnings).
2. Deploy to VM or full `build-rootfs.sh` if overlay/systemd changed.
3. In VNC: drag window, min/max/close, right-click terminal, minimize → taskbar restore.
4. Open second terminal — two taskbar icons, no crash, single title bar each.
5. Confirm `br8-panel` stays running: `ps aux | grep br8-panel` in guest.

## References

- User-facing docs: [backroot8/README.md](backroot8/README.md)
- Arch packages in image: `linux`, `xorg-server`, `xorg-xinit`, `xterm`, `nettle`, `networkmanager`, `openssh`
- Active development branch (example): `cursor/backroot-8-4238`
