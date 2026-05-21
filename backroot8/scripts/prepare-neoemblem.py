#!/usr/bin/env python3
"""Trim and downscale ASCII emblem text for neofetch (terminal aspect aware)."""
import sys
from pathlib import Path

# Terminal glyphs are ~2x taller than wide; keep the emblem slender on screen.
MAX_W = 26
MAX_H = 30
TERM_ASPECT = 2.0
EMBLEM_CH = "5"


def trim(lines):
    while lines and not lines[0].strip():
        lines.pop(0)
    while lines and not lines[-1].strip():
        lines.pop()
    if not lines:
        return []
    w = max(len(line) for line in lines)
    left = w
    right = 0
    for line in lines:
        for i, ch in enumerate(line):
            if ch.strip():
                left = min(left, i)
                right = max(right, i)
    return [line[left : right + 1] for line in lines]


def fit_dimensions(w, h, max_w=MAX_W, max_h=MAX_H, term_aspect=TERM_ASPECT):
    """Pick output size that preserves visual proportions on a terminal."""
    ratio = (w / h) / term_aspect
    out_h = min(max_h, h)
    out_w = min(max_w, max(8, round(out_h * ratio)))
    if out_w >= max_w:
        out_w = max_w
        out_h = min(max_h, max(8, round(out_w * term_aspect * h / w)))
    return out_w, out_h


def merge_block(block, fill=EMBLEM_CH):
    if not block:
        return " "
    if fill in block:
        return fill
    return block[len(block) // 2]


def downscale(lines, max_w=MAX_W, max_h=MAX_H):
    if not lines:
        return []
    h = len(lines)
    w = max(len(line) for line in lines)
    lines = [line.ljust(w) for line in lines]
    if w <= max_w and h <= max_h:
        return [line.rstrip() for line in lines]

    out_w, out_h = fit_dimensions(w, h, max_w, max_h)
    sy = h / out_h
    sx = w / out_w
    out = []
    for j in range(out_h):
        y0 = int(j * sy)
        y1 = max(y0 + 1, int((j + 1) * sy))
        row_parts = []
        for y in range(y0, min(y1, h)):
            row = lines[y]
            cols = []
            for i in range(out_w):
                x0 = int(i * sx)
                x1 = max(x0 + 1, int((i + 1) * sx))
                cols.append(merge_block(row[x0:x1]))
            row_parts.append("".join(cols))
        # merge stacked rows: keep emblem char if any row has it in column
        merged = []
        for i in range(out_w):
            col = "".join(r[i] if i < len(r) else " " for r in row_parts)
            merged.append(merge_block(col))
        out.append("".join(merged).rstrip())
    return out


def center(lines):
    if not lines:
        return []
    mw = max(len(line) for line in lines)
    return [line.center(mw) for line in lines]


def main():
    if len(sys.argv) != 3:
        sys.stderr.write(f"usage: {sys.argv[0]} input.txt output.txt\n")
        sys.exit(1)
    src, dst = Path(sys.argv[1]), Path(sys.argv[2])
    lines = src.read_text().splitlines()
    lines = trim(lines)
    lines = downscale(lines)
    lines = center(lines)
    dst.write_text("\n".join(lines) + "\n")
    sys.stderr.write(f"neoemblem: {len(lines)} lines x {max(len(l) for l in lines)} cols\n")


if __name__ == "__main__":
    main()
