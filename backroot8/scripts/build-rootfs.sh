#!/usr/bin/env bash
# Backward compatibility: old name for build-root.sh (VM disk images removed).
set -euo pipefail
echo "[backroot8] build-rootfs.sh is deprecated; use build-root.sh + build-iso.sh." >&2
exec "$(cd "$(dirname "$0")" && pwd)/build-root.sh" "$@"
