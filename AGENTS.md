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
| **Feature work** | `cursor/<name>-01a3` (cloud agent convention) |

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
    ▲ guest x11vnc :5900 ──SSH tunnel──▶ host :5903
    ▲ websockify ──────────────────────▶ noVNC :6080
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
| `_BR8_METRO_READY` | App on root | CARDINAL `1` after first frame drawn — start splash dismisses |
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
│   ├── verify-iso-boot.sh   # Headless QEMU boot test
│   ├── run-vm.sh            # QEMU from ISO + VNC
│   └── run-vm-gui.sh        # run-vm + guest x11vnc + noVNC :6080
└── vm/                      # Gitignored build artifacts
    ├── rootfs/
    ├── backroot8-live.iso
    ├── novnc.url            # Written by run-vm-gui.sh
    ├── qemu.pid
    └── websockify.pid
```

## noVNC + live ISO (agent playbook)

Use this section whenever you need a browser-testable desktop. **Do not reinvent the steps** — run the script from repo root.

### What `run-vm-gui.sh` does

1. Boots QEMU from `backroot8/vm/backroot8-live.iso` (creates `vm/qemu.pid`).
2. Waits for guest SSH on **host port 2222** (password `backroot8`).
3. Starts **x11vnc** inside the guest on `:0`, forwards it to **127.0.0.1:5903**.
4. Runs **websockify** + noVNC on **0.0.0.0:6080** → VNC port 5903.
5. Writes the browser URL to `backroot8/vm/novnc.url`.

QEMU also exposes VGA VNC on **5902** as a fallback; the script prefers the guest desktop tunnel when SSH is up.

### Quick status (run from repo root)

```bash
# ISO present?
test -f backroot8/vm/backroot8-live.iso && echo "ISO ok" || echo "BUILD ISO FIRST"

# noVNC up?
curl -s -o /dev/null -w '%{http_code}\n' http://127.0.0.1:6080/

# QEMU up?
test -f backroot8/vm/qemu.pid && ps -p "$(cat backroot8/vm/qemu.pid)" -o comm= 2>/dev/null | grep -q qemu-system && echo "QEMU ok"
```

Expect **`200`** from curl when noVNC is serving.

### First-time setup (no ISO yet)

From **repository root** (`/workspace` or clone root):

```bash
mkdir -p backroot8/vm
cd backroot8
sudo ./scripts/install-iso-build-deps.sh   # once per host
sudo ./scripts/build-root.sh               # vm/rootfs/ — slow (pacstrap)
sudo ./scripts/build-iso.sh                # vm/backroot8-live.iso
cd ..
./backroot8/scripts/run-vm-gui.sh
```

- Requires **network** and **sudo**.
- `vm/` must exist before bootstrap download.
- Chroot uses `DisableSandbox` in pacman.conf (Landlock fails on some hosts).
- Kernel cmdline `backroot8iso` triggers mkinitcpio hooks in `rootfs-overlay/etc/initcpio/`.
- **No KVM** in Cloud Agent VMs — TCG boot is slow (several minutes to desktop).

### Every task after code changes

| Change type | Action |
|-------------|--------|
| WM/panel C only | Hot-deploy (below); keep VM running |
| `rootfs-overlay/`, packages, initcpio | Rebuild rootfs + ISO, then restart QEMU |
| Broken/black session | `FORCE=1 ./backroot8/scripts/run-vm-gui.sh` |

**End state:** `./backroot8/scripts/run-vm-gui.sh` has been run and `curl` returns **200** on port **6080**. Leave it running unless you had to rebuild the ISO.

### Browser URL

**On the agent machine:**

```text
http://127.0.0.1:6080/vnc.html?autoconnect=1&resize=scale&path=websockify
```

Also in `backroot8/vm/novnc.url` after `run-vm-gui.sh`.

**Cursor / remote developers:** open the forwarded port from the **Ports** tab (forward **6080**). Do not use `localhost` on your own PC. If autoconnect fails over HTTPS, use the hash URL printed by the script (`encrypt=1`).

### Restart QEMU (after ISO rebuild or wedged guest)

```bash
pkill -f 'qemu-system-x86_64.*-name backroot8' || true
rm -f backroot8/vm/qemu.pid backroot8/vm/websockify.pid backroot8/vm/vnc-tunnel.pid
./backroot8/scripts/run-vm-gui.sh
# or: FORCE=1 ./backroot8/scripts/run-vm-gui.sh
```

### Hot-deploy binaries (WM/panel only — no ISO rebuild)

VM must already be running with SSH reachable:

```bash
sshpass -p backroot8 ssh -p 2222 -o StrictHostKeyChecking=no root@127.0.0.1 \
  'systemctl stop backroot8-desktop'
scp -P 2222 -o StrictHostKeyChecking=no \
  backroot8/src/br8-wm/br8-wm backroot8/src/br8-panel/br8-panel \
  root@127.0.0.1:/usr/local/bin/
sshpass -p backroot8 ssh -p 2222 -o StrictHostKeyChecking=no root@127.0.0.1 \
  'systemctl start backroot8-desktop'
```

Guest SSH: `ssh -p 2222 root@127.0.0.1` — password **`backroot8`**.

### noVNC troubleshooting

| Symptom | Check | Fix |
|---------|--------|-----|
| `ISO not found` | `ls backroot8/vm/backroot8-live.iso` | Run `build-root.sh` + `build-iso.sh` |
| `build-root.sh` failed at `br8-wm` | `ft2build.h` / empty `CFLAGS` | Host needs `libxcursor-dev` (see `install-iso-build-deps.sh`). Finish rootfs with the [recovery commands](#recover-after-partial-build-root) below |
| Guest **emergency mode** / SSH hangs | `tail vm/serial.log` | Initramfs likely stale: run [recovery commands](#recover-after-partial-build-root) |
| curl not `200` | `cat backroot8/vm/websockify.log` | Re-run `run-vm-gui.sh`; install `novnc` + `websockify` if missing |
| Blank/no desktop | Wait 3–5 min (TCG); or SSH in | `systemctl status backroot8-desktop`; see x11vnc warning in script output |
| VGA only (script warns) | Guest X not up yet | Wait; confirm `/tmp/.X11-unix/X0` via SSH |
| Stale QEMU | `vm/qemu.pid` zombie | `pkill` + `FORCE=1` as above |

Host packages for GUI testing (if not already installed): `qemu-system-x86`, `novnc`, `websockify`, `sshpass` — `install-iso-build-deps.sh` covers ISO build tools; `run-vm-gui.sh` can `apt-get install` novnc/sshpass on demand.

### Recover after partial `build-root.sh`

If `build-root.sh` exits during `sync-overlay` (common when `libxcursor-dev` is missing), the rootfs and initramfs are incomplete. From `backroot8/`:

```bash
sudo ./scripts/sync-overlay.sh ./vm/rootfs
sudo arch-chroot ./vm/rootfs systemctl enable \
  plymouth-start.service backroot8-fb-splash.service backroot8-live-cow.service \
  backroot8-desktop.service sshd
sudo arch-chroot ./vm/rootfs mkinitcpio -P
sudo ./scripts/build-iso.sh
cd .. && ./backroot8/scripts/run-vm-gui.sh
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

Then start the GUI test environment: `./backroot8/scripts/run-vm-gui.sh` (see [noVNC + live ISO](#novnc--live-iso-agent-playbook)).

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
5. Leave noVNC up when finishing unless restart is required ([playbook](#novnc--live-iso-agent-playbook)).

## References

- [backroot8/README.md](backroot8/README.md) — quick start
- [backroot8/RELEASE-MILESTONE1.md](backroot8/RELEASE-MILESTONE1.md) — USB, release workflow
- Packages: `packages.backroot8.txt` (`linux`, `xorg-server`, `xterm`, `pcmanfm`, …)

## Cursor Cloud

### Host dependencies

`gcc`, `libx11-dev`, `libxft-dev`, `libxcursor-dev`, `pkg-config`, `qemu-system-x86`, `arch-install-scripts`, `novnc`, `websockify`, `sshpass`, `zstd` — ISO build via `install-iso-build-deps.sh`; GUI via `run-vm-gui.sh`.

### Workflow (copy-paste)

```bash
# 1. Binaries (when editing WM/panel)
make -C backroot8/src/br8-wm && make -C backroot8/src/br8-panel

# 2. ISO (first time or overlay/package changes)
mkdir -p backroot8/vm && cd backroot8
sudo ./scripts/install-iso-build-deps.sh
sudo ./scripts/build-root.sh && sudo ./scripts/build-iso.sh
cd ..

# 3. Desktop in browser — required before finishing most UI tasks
./backroot8/scripts/run-vm-gui.sh
curl -s -o /dev/null -w '%{http_code}\n' http://127.0.0.1:6080/   # expect 200
```

### Gotchas

- QEMU `-vga std` needs Xorg **modesetting**, not vesa (see `rootfs-overlay/etc/X11/xorg.conf.d/10-vesa.conf`).
- `xinitrc` must be executable in the guest overlay.
- Guest SSH: `PermitRootLogin yes` in overlay.
- No KVM in Cloud Agent VMs — TCG is slow but works; allow several minutes after `run-vm-gui.sh`.
- Create `backroot8/vm/` before first bootstrap download.
