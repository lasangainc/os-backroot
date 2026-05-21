#!/usr/bin/env bash
# Composite boot splash PNG on a black canvas for framebuffer / feh display.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${1:-$ROOT/assets/bootscreen.png}"
OUT="${2:-$ROOT/rootfs-overlay/usr/share/backroot8/bootscreen.png}"

if ! command -v python3 >/dev/null; then
    echo "prepare-bootscreen.sh: python3 required" >&2
    exit 1
fi

python3 - "$SRC" "$OUT" <<'PY'
import sys
from pathlib import Path
from PIL import Image

src, out = Path(sys.argv[1]), Path(sys.argv[2])
im = Image.open(src).convert("RGBA")
w, h = im.size
bg = Image.new("RGB", (w, h), (0, 0, 0))
bg.paste(im, (0, 0), im)
out.parent.mkdir(parents=True, exist_ok=True)
bg.save(out, format="PNG")
print(f"Wrote {out} ({w}x{h} on black)")
PY
