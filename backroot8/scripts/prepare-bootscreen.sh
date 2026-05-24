#!/usr/bin/env bash
# Composite boot splash + Plymouth emblem (Windows-style placement: ~36% from top).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${1:-$ROOT/assets/bootscreen.png}"
OUT="${2:-$ROOT/rootfs-overlay/usr/share/backroot8/bootscreen.png}"
PPM_OUT="${OUT%.png}.ppm"
THEME_DIR="$ROOT/rootfs-overlay/usr/share/plymouth/themes/backroot8"
THEME_BOOTSCREEN="$THEME_DIR/bootscreen.png"
THEME_EMBLEM="$THEME_DIR/emblem.png"

if ! command -v python3 >/dev/null; then
    echo "prepare-bootscreen.sh: python3 required" >&2
    exit 1
fi

python3 - "$SRC" "$OUT" "$PPM_OUT" "$THEME_BOOTSCREEN" "$THEME_EMBLEM" <<'PY'
import struct
import sys
from pathlib import Path
from PIL import Image

src, out, ppm_out, theme_boot, theme_emblem = map(Path, sys.argv[1:6])
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
# Plymouth/fb viewers need opaque 24-bit RGB PNGs (no palette / no alpha-only assets).
bg = bg.convert("RGB")

out.parent.mkdir(parents=True, exist_ok=True)
bg.save(out, format="PNG", optimize=False, compress_level=6)
bg.save(ppm_out, format="PPM")

theme_boot.parent.mkdir(parents=True, exist_ok=True)
bg.save(theme_boot, format="PNG", optimize=False, compress_level=6)
emblem.save(theme_emblem, format="PNG")

# Validate PNG IHDR: 8-bit truecolor RGB (color type 2).
with out.open("rb") as f:
    if f.read(8) != b"\x89PNG\r\n\x1a\n":
        raise SystemExit(f"{out}: not a PNG after write")
    while True:
        raw = f.read(8)
        if len(raw) < 8:
            raise SystemExit(f"{out}: truncated PNG")
        length, ctype = struct.unpack(">I", raw[:4])[0], raw[4:8]
        data = f.read(length)
        f.read(4)
        if ctype == b"IHDR":
            if length < 13:
                raise SystemExit(f"{out}: bad IHDR")
            _w, _h, bit_depth, color_type = struct.unpack(">IIBB", data[:10])
            if bit_depth != 8 or color_type != 2:
                raise SystemExit(
                    f"{out}: expected 8-bit RGB PNG, got depth={bit_depth} type={color_type}"
                )
            break

print(f"Wrote {out} ({CANVAS_W}x{CANVAS_H}, logo {nw}x{nh} @ ({logo_x},{logo_y}))")
print(f"Wrote {ppm_out} (PPM fallback for fbi)")
print(f"Wrote {theme_boot}")
print(f"Wrote {theme_emblem} ({nw}x{nh} RGBA)")
PY

if command -v file >/dev/null; then
    file "$OUT" | grep -qi 'PNG image' || { echo "prepare-bootscreen.sh: $OUT is not a PNG" >&2; exit 1; }
fi
