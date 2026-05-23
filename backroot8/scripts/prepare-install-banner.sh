#!/usr/bin/env bash
# Fetch install wizard banner from Google Drive into rootfs-overlay.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/rootfs-overlay/usr/share/backroot8/install-banner.png"
FILE_ID="1Z367x3zAyPH-eefMLm7Xmtk9nNfhB_wV"
URL="https://drive.google.com/uc?export=download&id=${FILE_ID}"

mkdir -p "$(dirname "$OUT")"
curl -fsSL -o "$OUT" "$URL"
file "$OUT" | grep -qi 'PNG image' || { echo "prepare-install-banner: not a PNG" >&2; exit 1; }
echo "[prepare-install-banner] $OUT ($(du -h "$OUT" | awk '{print $1}'))"
