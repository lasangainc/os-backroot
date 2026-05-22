#!/usr/bin/env bash
# Composite boot splash emblem centered on a black canvas (framebuffer / feh).
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

# Standard splash canvas; fbi/feh scale this to the display while keeping aspect.
CANVAS_W, CANVAS_H = 1920, 1080
# Emblem occupies at most this fraction of the shorter canvas edge.
MAX_FRAC = 0.22
# Slightly above vertical center (typical OEM boot logo placement).
CENTER_Y_FRAC = 0.42

bbox = emblem.getbbox()
if bbox:
    emblem = emblem.crop(bbox)

ew, eh = emblem.size
max_dim = int(min(CANVAS_W, CANVAS_H) * MAX_FRAC)
scale = min(max_dim / ew, max_dim / eh, 1.0)
nw = max(1, int(ew * scale))
nh = max(1, int(eh * scale))
if (nw, nh) != (ew, eh):
    emblem = emblem.resize((nw, nh), Image.Resampling.LANCZOS)

bg = Image.new("RGB", (CANVAS_W, CANVAS_H), (0, 0, 0))
x = (CANVAS_W - nw) // 2
y = int(CANVAS_H * CENTER_Y_FRAC) - nh // 2
bg.paste(emblem, (x, y), emblem)

out.parent.mkdir(parents=True, exist_ok=True)
bg.save(out, format="PNG")
print(f"Wrote {out} ({CANVAS_W}x{CANVAS_H}, emblem {nw}x{nh} at ({x},{y}))")
PY
