#!/usr/bin/env bash
# Reassemble and decompress the Milestone 1 x86_64 disk image from GitHub Release parts.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VM_DIR="$ROOT/vm"
OUT_RAW="$VM_DIR/backroot8-x86_64.img"
PART_GLOB="${1:-$VM_DIR/backroot8-milestone1-x86_64.img.zst.part-*}"

if ! ls $PART_GLOB >/dev/null 2>&1; then
    echo "Usage: $0 [path/to/backroot8-milestone1-x86_64.img.zst.part-*]" >&2
    echo "Download all release parts into $VM_DIR first." >&2
    exit 1
fi

ZST="$VM_DIR/backroot8-x86_64.img.zst"
echo "[restore] Combining parts..."
cat $PART_GLOB >"$ZST"
echo "[restore] Decompressing to $OUT_RAW (needs ~6GB free)..."
zstd -d -f "$ZST" -o "$OUT_RAW"
ln -sfn backroot8-x86_64.img "$VM_DIR/backroot8.img"
echo "[restore] Done: $OUT_RAW"
echo "Run VM: $ROOT/scripts/run-vm-gui.sh"
