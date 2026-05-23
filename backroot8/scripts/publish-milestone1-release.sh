#!/usr/bin/env bash
# Build M1 ISO, verify boot, and upload to GitHub release v8-milestone1.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ISO="$ROOT/vm/backroot8-live.iso"
ASSET_NAME="backroot8-milestone1.iso"
RELEASE_TAG="${RELEASE_TAG:-v8-milestone1}"
REPO="${GITHUB_REPO:-lasangainc/os-backroot}"

log() { echo "[publish-m1] $*"; }

if [[ "$(id -u)" -ne 0 ]]; then
    exec sudo --preserve-env=GITHUB_REPO,RELEASE_TAG "${BASH_SOURCE[0]}" "$@"
fi

export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

command -v gh >/dev/null || { log "gh CLI required"; exit 1; }
command -v mksquashfs >/dev/null || "$ROOT/scripts/install-iso-build-deps.sh"

log "Building rootfs and ISO..."
"$ROOT/scripts/build-root.sh"
"$ROOT/scripts/build-iso.sh"

if [[ ! -f "$ISO" ]]; then
    log "ISO missing: $ISO"
    exit 1
fi

log "Verifying ISO boots..."
if ! "$ROOT/scripts/verify-iso-boot.sh"; then
    log "Boot verify failed; not uploading"
    exit 1
fi

STAGING="$(mktemp -d)"
cp "$ISO" "$STAGING/$ASSET_NAME"
chown "${SUDO_USER:-root}:${SUDO_USER:-root}" "$STAGING/$ASSET_NAME" 2>/dev/null || true

log "Uploading $ASSET_NAME to $RELEASE_TAG..."
sudo -u "${SUDO_USER:-root}" gh release upload "$RELEASE_TAG" \
    "$STAGING/$ASSET_NAME" \
    --repo "$REPO" \
    --clobber

rm -rf "$STAGING"
log "Done: https://github.com/$REPO/releases/tag/$RELEASE_TAG"
