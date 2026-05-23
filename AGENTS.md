# AGENTS.md — Backroot 8

Guide for AI agents working on **os-backroot** / **Backroot 8**: a minimal X11 desktop on Arch Linux, developed and tested by booting the **live ISO** in QEMU.

## Project summary

| Item | Detail |
|------|--------|
| **Goal** | Linux distro with custom DE on Arch (`linux` kernel package) |
| **WM** | `br8-wm` — C, Xlib only |
| **Panel** | `br8-panel` — C, Xlib only |
| **Session** | `startx` → `xinitrc` → panel + xterm + `exec br8-wm` |
| **Default branch** | `main` |
| **Feature work** | `cursor/<name>-4238` (cloud agent convention) |

All product code lives under **`backroot8/`**. Repo root points to [`backroot8/README.md`](backroot8/README.md).

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│  QEMU boots vm/backroot8-live.iso (BIOS/UEFI hybrid)  │
│  ┌───────────────────────────────────────────────────┐  │
│  │  initramfs: squashfs root + tmpfs overlay (live)  │  │
│  │  Xorg :0 (modesetting on QEMU -vga std)           │  │
│  │   ├── br8-panel (override_redirect, bottom)       │  │
│  │   ├── br8-wm — framed client windows              │  │
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
| `_BR8_METRO` | App on client | CARDINAL `1` — metro fullscreen app (no chrome) |
| `_BR8_METRO_ACTIVE` | WM on root | CARDINAL `1` while any metro app is mapped (hides panel) |
| `_BR8_START_OPEN` | Panel / br8-start | CARDINAL `0/1` — start menu visible |
| `_NET_CLIENT_LIST` | WM on root | EWMH client list |

**Taskbar restore:** Panel sets `_BR8_ACTIVATE` on root **and** `XMapWindow`/`XMapSubwindows` on the frame. WM handles `PropertyNotify` on root. Do **not** rely on `ClientMessage` to root alone.

**Minimize / restore:** Never `add_client()` on an existing frame or chrome window. `is_our_chrome()` must use valid `XQueryTree` children pointers (never `NULL` for children).

## Directory map

```
backroot8/
├── src/br8-wm/              # Window manager
├── src/br8-panel/           # Taskbar
├── rootfs-overlay/          # Session, initcpio hooks, systemd units
├── packages.backroot8.txt   # Pacman list for live rootfs
├── scripts/
│   ├── install-iso-build-deps.sh  # Host tools (Ubuntu/Debian)
│   ├── build-root.sh        # vm/rootfs/ (pacstrap + overlay)
│   ├── build-iso.sh         # vm/backroot8-live.iso
│   ├── sync-overlay.sh      # Binaries + overlay into rootfs
│   ├── verify-iso-boot.sh     # Headless QEMU boot test
│   ├── run-vm.sh            # QEMU from ISO + VNC
│   └── run-vm-gui.sh        # run-vm + noVNC :6080
└── vm/                      # Gitignored build artifacts
    ├── rootfs/
    └── backroot8-live.iso
```

## Common tasks

### Build desktop binaries (host)

```bash
make -C backroot8/src/br8-wm
make -C backroot8/src/br8-panel
make -C backroot8/src/power-pdf
```

Requires: `gcc`, `libx11-dev`, `libxft-dev`, `pkg-config`. PowerPDF also needs `poppler-glib` and `cairo`.

### Live ISO (first time / package or overlay changes)

```bash
mkdir -p backroot8/vm
cd backroot8
sudo ./scripts/install-iso-build-deps.sh   # once on Ubuntu/Debian
sudo ./scripts/build-root.sh               # vm/rootfs/
sudo ./scripts/build-iso.sh                # vm/backroot8-live.iso
```

- Network + `sudo` required; Arch bootstrap tarball auto-downloads to `vm/`.
- Chroot uses `DisableSandbox` in `pacman.conf` (Landlock fails on some hosts).
- Kernel cmdline `backroot8iso` triggers mkinitcpio hooks in `rootfs-overlay/etc/initcpio/`.

### Run VM for manual testing

```bash
./backroot8/scripts/run-vm-gui.sh
```

Browser: `http://localhost:6080/vnc.html?autoconnect=1&resize=scale`  
SSH: `ssh -p 2222 root@localhost` password `backroot8`

### Post-task: noVNC for developer testing

1. **Goal:** QEMU running with noVNC on port **6080**.
2. **If already up** and guest reflects your changes (hot-deploy), do not restart unnecessarily.
3. **If not running:** `./backroot8/scripts/run-vm-gui.sh` (requires existing ISO; build first if missing).
4. **Restart QEMU** after ISO rebuild or broken session:

```bash
pkill -f 'qemu-system-x86_64.*-name backroot8' || true
rm -f backroot8/vm/qemu.pid backroot8/vm/websockify.pid
./backroot8/scripts/run-vm-gui.sh
```

5. In PRs, include the test URL above.

Quick check: `curl -s -o /dev/null -w '%{http_code}' http://127.0.0.1:6080/` → `200`.

### Hot-deploy binaries into running VM

```bash
sshpass -p backroot8 ssh -p 2222 root@127.0.0.1 'systemctl stop backroot8-desktop'
scp backroot8/src/br8-wm/br8-wm backroot8/src/br8-panel/br8-panel root@127.0.0.1:/usr/local/bin/
ssh -p 2222 root@127.0.0.1 'systemctl start backroot8-desktop'
```

## UI behavior (do not regress)

1. **Title bar:** App name from `_NET_WM_NAME` / `WM_NAME` / `WM_CLASS`, **centered**; buttons on the right.
2. **Close:** Elongated X inside **red square** border (`draw_close_button`).
3. **Right-click root:** **New terminal** and **file manager** (pcmanfm); hovered row highlighted before click.
4. **Taskbar:** Slightly transparent (alpha blend), **not blurred**; app badges; click restores minimized windows.
5. **Minimize:** Unmaps frame; taskbar click must restore without duplicate decorations.

## Known pitfalls

| Issue | Cause | Fix |
|-------|--------|-----|
| Taskbar missing | `br8-panel` crashed on bad `_NET_WM_ICON` | `icon_fallback()`; `XSetErrorHandler(NULL)` |
| Stacked title bars | `MapRequest` on frame re-wrapped | `is_our_chrome()` + skip `add_client` |
| WM core dump | `XQueryTree(..., NULL, NULL)` | Always pass `&children`, `&nch`, then `XFree` |
| Taskbar click no-op | `ClientMessage` to root ignored | `_BR8_ACTIVATE` + direct `XMapWindow` |
| ISO boot hang / no root | busybox `mount` lacks overlay | `backroot8_root` install hook adds `/usr/bin/mount` |
| Blank desktop in QEMU | `vesa` vs kernel framebuffer | Xorg **modesetting** in `10-vesa.conf` |
| `xfce4-terminal` broken | SONAME skew | Use `xterm` |

## Code conventions

- **C / X11:** Minimal dependencies (Xlib only). No compositor, no toolkit.
- **Scope:** Smallest correct diff; match existing style.
- **Do not commit:** `vm/` artifacts, compiled binaries under `src/*/`.
- **Windows 8 styling:** Only when explicitly requested in the task.

## Verification checklist

After WM/panel or overlay changes:

1. `make` WM and panel without new warnings.
2. `sudo ./scripts/build-root.sh && sudo ./scripts/build-iso.sh` if overlay/systemd/initcpio changed; else hot-deploy.
3. `./scripts/verify-iso-boot.sh` when initramfs or ISO layout changed.
4. In noVNC: drag, min/max/close, root menu, minimize → taskbar restore; two terminals → two icons.
5. Leave noVNC up when finishing unless restart is required.

## References

- [backroot8/README.md](backroot8/README.md) — quick start
- [backroot8/RELEASE-MILESTONE1.md](backroot8/RELEASE-MILESTONE1.md) — USB, release workflow
- Packages: `packages.backroot8.txt` (`linux`, `xorg-server`, `xterm`, `pcmanfm`, …)

## Cursor Cloud

### Host dependencies

`gcc`, `libx11-dev`, `libxft-dev`, `pkg-config`, `qemu-system-x86`, `arch-install-scripts`, `novnc`, `sshpass`, `zstd` (install script: `install-iso-build-deps.sh`).

### Workflow

1. `make -C backroot8/src/br8-wm` and `br8-panel` (zero warnings).
2. `mkdir -p backroot8/vm && sudo ./scripts/install-iso-build-deps.sh && sudo ./scripts/build-iso.sh`
3. `./backroot8/scripts/run-vm-gui.sh`
4. End tasks with noVNC available at port **6080**.

### Gotchas

- QEMU `-vga std` needs Xorg **modesetting**, not vesa (see `rootfs-overlay/etc/X11/xorg.conf.d/10-vesa.conf`).
- `xinitrc` must be executable in the guest overlay.
- Guest SSH: `PermitRootLogin yes` in overlay.
- No KVM in Cloud Agent VMs — TCG is slow but works.
- Create `backroot8/vm/` before first bootstrap download.
