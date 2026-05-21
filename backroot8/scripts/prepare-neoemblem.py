#!/usr/bin/env python3
"""Trim and downscale ASCII emblem text for neofetch."""
import sys
from pathlib import Path

MAX_W = 40
MAX_H = 22


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


def downscale(lines, max_w=MAX_W, max_h=MAX_H):
    if not lines:
        return []
    h = len(lines)
    w = max(len(line) for line in lines)
    lines = [line.ljust(w) for line in lines]
    if w <= max_w and h <= max_h:
        return [line.rstrip() for line in lines]
    out_h = min(max_h, h)
    out_w = min(max_w, w)
    sy = h / out_h
    sx = w / out_w
    out = []
    for j in range(out_h):
        y = min(h - 1, int(j * sy + sy / 2 - 0.5))
        row = []
        for i in range(out_w):
            x = min(w - 1, int(i * sx + sx / 2 - 0.5))
            row.append(lines[y][x])
        out.append("".join(row).rstrip())
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
