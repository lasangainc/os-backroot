#!/usr/bin/env bash
# Install Win7OS-cursors (GPL-3.0) into rootfs-overlay for Backroot 8.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$ROOT/rootfs-overlay/usr/share/icons/Win7OS-cursors"
REPO_URL="https://github.com/yeyushengfan258/Win7OS-cursors.git"
CACHE="${ROOT}/vm/.cache/win7os-cursors"

log() { echo "[prepare-win7-cursors] $*"; }

if [[ -f "$DEST/index.theme" && -d "$DEST/cursors" ]]; then
    log "Already present: $DEST"
    exit 0
fi

mkdir -p "$(dirname "$CACHE")"
if [[ ! -d "$CACHE/.git" ]]; then
    log "Cloning $REPO_URL ..."
    git clone --depth 1 "$REPO_URL" "$CACHE"
fi

if [[ ! -d "$CACHE/dist/cursors" ]]; then
    log "ERROR: dist/cursors missing in $CACHE" >&2
    exit 1
fi

rm -rf "$DEST"
mkdir -p "$(dirname "$DEST")"
cp -a "$CACHE/dist" "$DEST"
install -Dm644 "$CACHE/LICENSE" "$DEST/COPYING"

log "Installed Win7OS-cursors to $DEST"
