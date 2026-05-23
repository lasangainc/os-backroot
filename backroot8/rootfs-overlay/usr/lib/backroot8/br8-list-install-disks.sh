#!/bin/bash
# List whole disks for the installer (tab-separated, one disk per line).
# Fields: DEV<TAB>SIZE<TAB>MODEL<TAB>VENDOR
set -euo pipefail

live_disk() {
    local m src pk
    for m in /run/backroot8iso /run/archiso /lib/live/mount/medium; do
        mountpoint -q "$m" 2>/dev/null || continue
        src="$(findmnt -no SOURCE "$m" 2>/dev/null || true)"
        [[ -n "$src" ]] || continue
        pk="$(lsblk -no PKNAME "$src" 2>/dev/null || true)"
        if [[ -n "$pk" ]]; then
            echo "/dev/$pk"
            return 0
        fi
    done
    return 1
}

LIVE="$(live_disk || true)"

lsblk -d -n -o NAME,SIZE,TYPE | while read -r name size type; do
    [[ "$type" == "disk" ]] || continue
    [[ "$name" == loop* || "$name" == ram* ]] && continue
    dev="/dev/$name"
    [[ -n "$LIVE" && "$dev" == "$LIVE" ]] && continue
    model="$(lsblk -d -n -o MODEL "$dev" 2>/dev/null | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')"
    vendor="$(lsblk -d -n -o VENDOR "$dev" 2>/dev/null | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')"
    [[ -z "$model" || "$model" == "-" ]] && model="Unknown model"
    [[ -z "$vendor" || "$vendor" == "-" ]] && vendor=""
    printf '%s\t%s\t%s\t%s\n' "$dev" "$size" "$model" "$vendor"
done
