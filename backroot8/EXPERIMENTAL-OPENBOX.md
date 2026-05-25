# Experimental: Openbox instead of br8-wm

This branch replaces the scratch window manager with **Openbox** plus a small **br8-metro-helper** companion.

## What changed

| Component | Role |
|-----------|------|
| **Openbox** + `Backroot8-Sharp` theme | Normal window decorations: centered title (`titleLayout` `    CMX`), elongated red close button, square corners |
| **br8-metro-helper** | `_BR8_METRO` fullscreen, `_BR8_METRO_ACTIVE`, charms bar + clock, swipe-from-bottom → start |
| **br8-panel** | EWMH task list when `BR8_EXTERNAL_WM` is set |
| **br8-start** | Unchanged |
| **br8-wm** | Still built/installed but not started by default |

## Revert to br8-wm

In `rootfs-overlay/etc/X11/xinit/xinitrc`:

- Remove `export BR8_EXTERNAL_WM=openbox`
- Remove `br8-metro-helper` and Openbox setup
- `exec /usr/local/bin/br8-wm`

## Theme

`scripts/prepare-openbox-theme.sh` generates `rootfs-overlay/usr/share/themes/Backroot8-Sharp/openbox-3/`.
