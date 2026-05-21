#!/usr/bin/env python3
"""Convert taskbar emblem PNG to neofetch ASCII art."""
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    sys.stderr.write("png-to-neoemblem.py: requires python3-pillow\n")
    sys.exit(1)

CHARS = " .'`^\",:;Il!i><~+_-?][}{1)(|\\/tfjrxnuvczXYUJQLKHFp896543#MW&8%B@$"
TARGET_W = 36
ASPECT = 0.55


def main():
    if len(sys.argv) != 3:
        sys.stderr.write(f"usage: {sys.argv[0]} input.png output.txt\n")
        sys.exit(1)
    src, dst = Path(sys.argv[1]), Path(sys.argv[2])
    im = Image.open(src).convert("RGBA")
    bg = Image.new("RGBA", im.size, (0, 0, 0, 255))
    im = Image.alpha_composite(bg, im).convert("L")
    w, h = im.size
    target_h = max(8, int(h / w * TARGET_W * ASPECT))
    im = im.resize((TARGET_W, target_h), Image.Resampling.LANCZOS)
    w, h = im.size
    lines = []
    for y in range(h):
        row = ""
        for x in range(w):
            lum = im.getpixel((x, y))
            idx = min(len(CHARS) - 1, int(lum / 255 * (len(CHARS) - 1)))
            row += CHARS[idx]
        lines.append(row.rstrip())
    while lines and not lines[0].strip():
        lines.pop(0)
    while lines and not lines[-1].strip():
        lines.pop()
    mw = max(len(line) for line in lines)
    lines = [line.center(mw) for line in lines]
    dst.write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
