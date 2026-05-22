#!/usr/bin/env bash
# Composite boot splash: small centered emblem on black canvas.
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
emblem = Image.open(src).convert("RGBA")

CANVAS_W, CANVAS_H = 1024, 768
LOGO_HEIGHT_FRAC = 0.11
LOGO_CENTER_Y_FRAC = 0.36

bbox = emblem.getbbox()
if bbox:
    emblem = emblem.crop(bbox)

ew, eh = emblem.size
target_h = max(1, int(CANVAS_H * LOGO_HEIGHT_FRAC))
scale = target_h / eh
nw = max(1, int(ew * scale))
nh = max(1, int(eh * scale))
if (nw, nh) != (ew, eh):
    emblem = emblem.resize((nw, nh), Image.Resampling.LANCZOS)

logo_x = (CANVAS_W - nw) // 2
logo_y = int(CANVAS_H * LOGO_CENTER_Y_FRAC) - nh // 2

bg = Image.new("RGB", (CANVAS_W, CANVAS_H), (0, 0, 0))
bg.paste(emblem, (logo_x, logo_y), emblem)

out.parent.mkdir(parents=True, exist_ok=True)
bg.save(out, format="PNG")
print(f"Wrote {out} ({CANVAS_W}x{CANVAS_H}, logo {nw}x{nh} @ ({logo_x},{logo_y}))")
PY
