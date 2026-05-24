#!/usr/bin/env bash
# Build Milestone 1.5 ISO, verify boot, and publish to GitHub release v8-milestone1.5.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ISO="$ROOT/vm/backroot8-live.iso"
ASSET_NAME="backroot8-milestone1.5.iso"
RELEASE_TAG="${RELEASE_TAG:-v8-milestone1.5}"
REPO="${GITHUB_REPO:-lasangainc/os-backroot}"
NOTES_FILE="$ROOT/RELEASE-v8-milestone1.5-notes.md"

log() { echo "[publish-m1.5] $*"; }

if [[ "$(id -u)" -ne 0 ]]; then
    exec sudo --preserve-env=GITHUB_REPO,RELEASE_TAG "${BASH_SOURCE[0]}" "$@"
fi

export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
export BACKROOT8_VERSION=8-milestone1.5

command -v gh >/dev/null || { log "gh CLI required"; exit 1; }
command -v mksquashfs >/dev/null || "$ROOT/scripts/install-iso-build-deps.sh"

log "Building rootfs and ISO (8-milestone1.5)..."
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

SHA256="$(sha256sum "$STAGING/$ASSET_NAME" | awk '{print $1}')"
log "SHA256: $SHA256"

RELEASE_BODY="$STAGING/release-body.md"
if [[ -f "$NOTES_FILE" ]]; then
    cp "$NOTES_FILE" "$RELEASE_BODY"
else
    cat > "$RELEASE_BODY" <<EOF
## Backroot 8 Milestone 1.5

Bootable hybrid live ISO for x86_64 (BIOS + UEFI).

### Download

- **$ASSET_NAME** — write to USB with \`dd\` or boot from optical media

### Login

\`root\` / \`backroot8\`

Build from source: \`backroot8/RELEASE-MILESTONE1.5.md\`
EOF
fi
{
    echo ""
    echo "### SHA256"
    echo '```'
    echo "$SHA256  $ASSET_NAME"
    echo '```'
    echo ""
    echo "### Commit"
    echo "\`$(git -C "$ROOT" rev-parse HEAD)\`"
} >> "$RELEASE_BODY"

GH_USER="${SUDO_USER:-root}"
if ! sudo -u "$GH_USER" gh release view "$RELEASE_TAG" --repo "$REPO" >/dev/null 2>&1; then
    log "Creating release $RELEASE_TAG..."
    sudo -u "$GH_USER" gh release create "$RELEASE_TAG" \
        --repo "$REPO" \
        --title "Backroot 8 Milestone 1.5" \
        --notes-file "$RELEASE_BODY"
fi

log "Uploading $ASSET_NAME to $RELEASE_TAG..."
sudo -u "$GH_USER" gh release upload "$RELEASE_TAG" \
    "$STAGING/$ASSET_NAME" \
    --repo "$REPO" \
    --clobber

if sudo -u "$GH_USER" gh release view "$RELEASE_TAG" --repo "$REPO" --json isPrerelease \
    | grep -q '"isPrerelease":true'; then
    sudo -u "$GH_USER" gh release edit "$RELEASE_TAG" --repo "$REPO" --prerelease=false
fi

rm -rf "$STAGING"
log "Done: https://github.com/$REPO/releases/tag/$RELEASE_TAG"
