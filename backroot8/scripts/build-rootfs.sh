#!/usr/bin/env bash
echo "[backroot8] build-rootfs.sh is deprecated (VM disk images removed)." >&2
echo "Use: sudo ./scripts/build-root.sh   # root filesystem" >&2
echo "     sudo ./scripts/build-iso.sh    # bootable live ISO" >&2
exec "$(cd "$(dirname "$0")" && pwd)/build-root.sh" "$@"
