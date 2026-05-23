# Handoff: buildingisopipe branch

**Branch:** `buildingisopipe`  
**Base:** `cursor/live-iso-43f1` (PR #24 — Milestone 1 ISO scaffolding)  
**Status:** Overlay boot fix in progress (`cursor/iso-overlay-boot-fix-4a43`) — util-linux `mount` in initramfs + tmpfs upperdir ordering.

## Goal

Deliver a bootable **Backroot 8 live ISO** (`vm/backroot8-live.iso`) that starts the full desktop (WM, panel, xterm). QEMU/noVNC is the dev loop; **VM disk images are deprecated**.

## What works

- **`sudo ./scripts/install-iso-build-deps.sh`** — Ubuntu host tools (`mksquashfs`, `grub-mkrescue`, compile deps).
- **`sudo ./scripts/build-root.sh`** — Arch root under `vm/rootfs/` from `packages.backroot8.txt` + `sync-overlay.sh`.
- **`sudo ./scripts/build-iso.sh`** — Produces `vm/backroot8-live.iso` (~1.1 GB) using **squashfs of rootfs** (no longer ext4-in-squashfs only).
- **Earlier verify** — Login on serial passed with old **ext4-in-squashfs + RAM copy** layout (needs ~4 GB RAM). That path may still exist in `backroot8_iso` hook as legacy fallback if `backroot8-root.img` exists on ISO.

## What is broken (user report + last verify)

**Symptom:** Blank screen + blinking cursor in noVNC (cursor resets ~every 3–15 s).

**Causes identified:**

1. **Read-only root (fixed in theory):** Old ISO mounted ext4 loop from **read-only** squashfs → systemd-logind/X failed. New approach: squashfs rootfs + **overlay** on tmpfs (`backroot8_root` hook).
2. **Overlay boot not passing verify yet:** Last serial log:
   ```
   mount: /new_root: special device overlay does not exist
   backroot8_root: overlay mount failed
   ```
   `overlay` was added to `MODULES` and `add_module` in initcpio install scripts; `mkinitcpio -v` shows `overlay.ko.zst` when run in chroot — ensure **ISO staging copies fresh** `initramfs-linux.img` after every `mkinitcpio -P`.
3. **noVNC vs VGA:** `run-vm-gui.sh` falls back to **QEMU VGA VNC** if guest x11vnc/X11 socket is not ready — VGA shows **tty/console**, not X. Even with working X, browser may look blank until x11vnc tunnel works.

## Uncommitted / WIP changes on this branch

| Path | Purpose |
|------|---------|
| `rootfs-overlay/etc/initcpio/hooks/backroot8_iso` | ISO mount; squashfs rootfs OR legacy RAM copy of `.img` |
| `rootfs-overlay/etc/initcpio/hooks/backroot8_root` | Overlay mount on `/new_root`; overrides `default_mount_handler` |
| `rootfs-overlay/etc/initcpio/install/backroot8_root` | `add_module overlay` |
| `rootfs-overlay/etc/initcpio/install/backroot8_iso` | `add_module overlay` |
| `scripts/build-iso.sh` | `mksquashfs "$ROOTFS"` directly; grub **without** `root=LABEL=backroot8` |
| `scripts/build-root.sh` | HOOKS include `backroot8_root`; `MODULES=(overlay …)`; enable `backroot8-live-cow` |
| `rootfs-overlay/etc/systemd/system/backroot8-live-cow.service` | `remount,rw` + `/var/lib/systemd` prep |
| `rootfs-overlay/usr/lib/backroot8/live-cow-setup.sh` | Live cow setup script |
| `rootfs-overlay/etc/X11/xorg.conf.d/10-vesa.conf` | QEMU vesa |
| `rootfs-overlay/etc/systemd/system/backroot8-desktop.service` | `Restart=on-failure`, `startx … vt1` |
| `packages.backroot8.txt` | **pcmanfm** instead of dolphin (smaller rootfs) |
| `src/br8-wm/br8-wm.c`, `src/br8-start/br8-start.c` | Spawn **pcmanfm** not dolphin |
| `scripts/verify-iso-boot.sh` | `RAM_MB=4096` default; overlay boot markers |
| `scripts/run-vm.sh` | `RAM_MB=4096`, ISO boot |

## Next agent checklist

1. **Confirm overlay in booted initramfs**
   ```bash
   sudo arch-chroot backroot8/vm/rootfs lsinitcpio -m /boot/initramfs-linux.img | grep overlay
   ```
   Rebuild ISO only after `mkinitcpio -P` inside properly mounted rootfs (`build-root.sh` does mounts).

2. **Fix overlay mount if still failing**
   - `modprobe overlay` in `backroot8_root` before `mount -t overlay`.
   - If module missing at boot, use `MODULES=(overlay)` and verify `modconf` hook packs `.ko.zst`.
   - Alternative: revert to **RAM copy** of ext4 img (`build-iso.sh` mkfs.ext4 + mksquashfs img) and require **4096 MB** RAM — was closer to working for login.

3. **Verify boot**
   ```bash
   cd backroot8 && sudo ./scripts/build-root.sh && sudo ./scripts/build-iso.sh
   ./scripts/verify-iso-boot.sh
   ```
   Expect serial: `backroot8_root: live root on overlay` and `backroot8 login:`.

4. **Verify desktop**
   ```bash
   ./scripts/run-vm-gui.sh
   # http://localhost:6080/vnc.html?autoconnect=1&resize=scale
   ```
   SSH: `ssh -p 2222 root@localhost` / `backroot8` — check `systemctl status backroot8-desktop`, `/tmp/.X11-unix/X0`, `journalctl -u backroot8-desktop`.

5. **If overlay path is too fragile**, consider **archiso** profile on Arch builder (documented in planning; not started).

## Build commands (quick reference)

```bash
cd backroot8
sudo ./scripts/install-iso-build-deps.sh   # once on Ubuntu
sudo ./scripts/build-root.sh
sudo ./scripts/build-iso.sh
./scripts/verify-iso-boot.sh
./scripts/run-vm-gui.sh
```

## PR note

`cursor/live-iso-43f1` / PR #24 has the **first** milestone commit; this branch stacks **debug/fix WIP** on top. Merge or supersede PR after overlay boot + desktop confirmed.

## Do not commit

- `backroot8/vm/` (gitignored artifacts)
- Compiled binaries under `src/*/br8-*`, `powerpdf`, etc.
