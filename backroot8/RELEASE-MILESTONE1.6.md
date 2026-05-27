# Backroot 8 Milestone 1.6

Bootable hybrid live ISO for BIOS and UEFI systems. Replaces **Milestone 1.5** (defective OOBE/installer regressions).

## Download

**[GitHub Release v8-milestone1.6v3](https://github.com/lasangainc/os-backroot/releases/tag/v8-milestone1.6v3)** — asset `backroot8-milestone1.6v3.iso` (current).

Previous: [v8-milestone1.6v2](https://github.com/lasangainc/os-backroot/releases/tag/v8-milestone1.6v2), [v8-milestone1.6](https://github.com/lasangainc/os-backroot/releases/tag/v8-milestone1.6)

> **Milestone 1.5 is defective.** Use **1.6v3** or [Milestone 1](https://github.com/lasangainc/os-backroot/releases/tag/v8-milestone1) instead.

## Fixes in 1.6 / 1.6v2

- Installer progress checkmarks render correctly (vector checkmarks, not missing UTF-8 glyphs)
- OOBE intro screen: “Personalize” / “Sign in” sized to fit on screen
- OOBE account creation: Finish works with passwords; validation errors shown inline
- Post-setup loading screen dismisses when the taskbar is ready (no metro/start-menu race)
- No automatic terminal window on login

### 1.6v2

- Start menu opens quickly (single `br8-start`, lazy wallpaper)

### 1.6v3 only

- OOBE loading runs account setup and desktop restart in the background
- `trigger-oobe-debug` on live ISO to test OOBE without installing

## Build

```bash
cd backroot8
sudo ./scripts/install-iso-build-deps.sh
sudo ./scripts/build-root.sh
BACKROOT8_VERSION=8-milestone1.6v3 sudo ./scripts/build-iso.sh
```

Writes `vm/backroot8-live.iso`. Publish:

```bash
sudo ./scripts/publish-milestone1.6v3-release.sh
```

### Live OOBE debug

On a running live session:

```bash
sudo trigger-oobe-debug
```

## Test in QEMU

```bash
./scripts/run-vm-gui.sh
```

**http://localhost:6080/vnc.html?autoconnect=1&resize=scale**

## Write to USB

```bash
sudo dd if=vm/backroot8-live.iso of=/dev/sdX bs=4M status=progress conv=fsync
```
