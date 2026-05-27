#!/usr/bin/env bash
# Build M1.6 ISO, verify boot, and upload to GitHub release v8-milestone1.6.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ISO="$ROOT/vm/backroot8-live.iso"
ASSET_NAME="backroot8-milestone1.6.iso"
RELEASE_TAG="${RELEASE_TAG:-v8-milestone1.6}"
REPO="${GITHUB_REPO:-lasangainc/os-backroot}"

log() { echo "[publish-m1.6] $*"; }

if [[ "$(id -u)" -ne 0 ]]; then
    exec sudo --preserve-env=GITHUB_REPO,RELEASE_TAG "${BASH_SOURCE[0]}" "$@"
fi

export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
export BACKROOT8_VERSION=8-milestone1.6

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

UPLOAD_ISO="/tmp/$ASSET_NAME"
cp "$ISO" "$UPLOAD_ISO"
chown "${SUDO_USER:-root}:${SUDO_USER:-root}" "$UPLOAD_ISO" 2>/dev/null || true

NOTES="$ROOT/RELEASE-v8-milestone1.6-notes.md"
if [[ ! -f "$NOTES" ]]; then
    log "Missing $NOTES"
    exit 1
fi

if ! sudo -u "${SUDO_USER:-root}" gh release view "$RELEASE_TAG" --repo "$REPO" &>/dev/null; then
    log "Creating release $RELEASE_TAG..."
    sudo -u "${SUDO_USER:-root}" gh release create "$RELEASE_TAG" \
        --repo "$REPO" \
        --title "Backroot 8 Milestone 1.6" \
        --notes-file "$NOTES"
fi

log "Uploading $ASSET_NAME to $RELEASE_TAG..."
sudo -u "${SUDO_USER:-root}" gh release upload "$RELEASE_TAG" \
    "$UPLOAD_ISO" \
    --repo "$REPO" \
    --clobber

rm -f "$UPLOAD_ISO"

log "Marking Milestone 1.5 as DEFECTIVE..."
if sudo -u "${SUDO_USER:-root}" gh release view v8-milestone1.5 --repo "$REPO" &>/dev/null; then
    DEFECT_BODY="$(mktemp)"
    {
        echo "## DEFECTIVE — do not use"
        echo ""
        echo "This release has broken OOBE, installer checkmarks, and login behavior."
        echo "Use **[Milestone 1.6](https://github.com/$REPO/releases/tag/v8-milestone1.6)** instead."
        echo ""
        echo "---"
        echo ""
        sudo -u "${SUDO_USER:-root}" gh release view v8-milestone1.5 --repo "$REPO" --json body -q .body 2>/dev/null || true
    } >"$DEFECT_BODY"
    sudo -u "${SUDO_USER:-root}" gh release edit v8-milestone1.5 \
        --repo "$REPO" \
        --title "Backroot 8 Milestone 1.5 (DEFECTIVE)" \
        --notes-file "$DEFECT_BODY"
    rm -f "$DEFECT_BODY"
fi

log "Done: https://github.com/$REPO/releases/tag/$RELEASE_TAG"
