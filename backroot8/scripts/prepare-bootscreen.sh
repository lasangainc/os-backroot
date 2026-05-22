#!/usr/bin/env bash
# Composite boot splash: small centered emblem + spinner (Windows-style layout).
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
from PIL import Image, ImageDraw

src, out = Path(sys.argv[1]), Path(sys.argv[2])
emblem = Image.open(src).convert("RGBA")

# Match typical QEMU std VGA / early framebuffer (fbi shows 1:1 when possible).
CANVAS_W, CANVAS_H = 1024, 768
# Logo height ~11% of screen (similar to Windows boot).
LOGO_HEIGHT_FRAC = 0.11
LOGO_CENTER_Y_FRAC = 0.36
SPINNER_SIZE = 28
GAP_LOGO_SPINNER = 36

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

spinner = Image.new("RGBA", (SPINNER_SIZE, SPINNER_SIZE), (0, 0, 0, 0))
draw = ImageDraw.Draw(spinner)
pad = 2
box = [pad, pad, SPINNER_SIZE - pad - 1, SPINNER_SIZE - pad - 1]
draw.ellipse(box, outline=(90, 90, 90, 200), width=2)
draw.arc(box, start=-90, end=200, fill=(255, 255, 255, 255), width=2)

block_h = nh + GAP_LOGO_SPINNER + SPINNER_SIZE
block_top = int(CANVAS_H * LOGO_CENTER_Y_FRAC) - block_h // 2
logo_x = (CANVAS_W - nw) // 2
logo_y = block_top
spin_x = (CANVAS_W - SPINNER_SIZE) // 2
spin_y = logo_y + nh + GAP_LOGO_SPINNER

bg = Image.new("RGB", (CANVAS_W, CANVAS_H), (0, 0, 0))
bg.paste(emblem, (logo_x, logo_y), emblem)
bg.paste(spinner, (spin_x, spin_y), spinner)

out.parent.mkdir(parents=True, exist_ok=True)
bg.save(out, format="PNG")
print(
    f"Wrote {out} ({CANVAS_W}x{CANVAS_H}, logo {nw}x{nh} @ ({logo_x},{logo_y}), "
    f"spinner @ ({spin_x},{spin_y}))"
)
PY
