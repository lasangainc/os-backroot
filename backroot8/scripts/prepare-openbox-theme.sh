#!/usr/bin/env bash
# Generate Backroot8-Sharp Openbox theme assets (elongated close button XPMs).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
THEME="$ROOT/rootfs-overlay/usr/share/themes/Backroot8-Sharp/openbox-3"
mkdir -p "$THEME"

python3 - "$THEME" <<'PY'
import sys
from pathlib import Path

theme = Path(sys.argv[1])

themerc = """# Backroot 8 Sharp — centered title, elongated close, square corners
border.width: 1
border.color: #2b2b2b
window.active.border.color: #2b2b2b
window.inactive.border.color: #2b2b2b
window.active.title.bg: flat
window.inactive.title.bg: flat
window.active.title.bg.color: #2b2b2b
window.inactive.title.bg.color: #2b2b2b
window.active.label.text.color: #ffffff
window.inactive.label.text.color: #c8c8d0
window.active.label.bg: parentrelative
window.inactive.label.bg: parentrelative
window.active.client.bg.color: #1e2030
window.inactive.client.bg.color: #1e2030
menu.title.bg: flat
menu.title.bg.color: #2b2b2b
menu.title.text.color: #e8e8ec
menu.bg.color: #2b2b2b
menu.text.color: #e8e8ec
menu.border.color: #4a4a58
menu.border.width: 1
menu.over.bg.color: #3a5a9a
menu.over.text.color: #ffffff
menu.active.bg.color: #3a5a9a
menu.active.text.color: #ffffff
menu.disabled.text.color: #808088
menu.separator.color: #4a4a58
menu.bullet.color: #e8e8ec
menu.bullet.selected.color: #ffffff
menu.bullet.unselected.color: #808088
padding.width: 0
padding.height: 0
window.handle.width: 4
window.client.padding.width: 0
window.client.padding.height: 0
title.vertical: false
title.height: 24
title.horizontal: true
title.text: center
titlebar.left:
titlebar.right: CMX
"""
(theme / "themerc").write_text(themerc)

w, h = 44, 24
pixels = []
for y in range(h):
    row = ""
    for x in range(w):
        if x < 1 or x >= w - 1 or y < 1 or y >= h - 1:
            row += "a"
        else:
            cx, cy = (w - 1) / 2.0, (h - 1) / 2.0
            if abs((x - cx) - (y - cy)) < 1.3 and abs(x - cx) < 7:
                row += "w"
            elif abs((x - cx) + (y - cy)) < 1.3 and abs(x - cx) < 7:
                row += "w"
            else:
                row += "r"
    pixels.append(row)

def write_close(path, red):
    lines = ["/* XPM */", "static char *close[] = {", f'"{w} {h} 4 1",',
             f'"a c #2b2b2b",', f'"r c {red}",', '"w c #ffffff",', '"x c #d04030",']
    for row in pixels:
        lines.append(f'"{row}",')
    lines.append("};")
    path.write_text("\n".join(lines) + "\n")

write_close(theme / "close.xpm", "#c42b1c")
write_close(theme / "close_hover.xpm", "#e84a38")
write_close(theme / "close_pressed.xpm", "#a02018")

for name in ("max", "max_hover", "max_pressed", "max_toggled", "max_toggled_hover",
             "max_toggled_pressed", "desk", "desk_hover", "desk_pressed",
             "iconify", "iconify_hover", "iconify_pressed"):
    tiny = "\n".join([
        "/* XPM */", f"static char *{name}[] = {{", '"10 10 2 1",',
        '"a c #2b2b2b",', '"b c #c8c8d0",',
    ] + ['"bbbbbbbbbb",' for _ in range(10)] + ["};", ""])
    (theme / f"{name}.xpm").write_text(tiny)

print(f"Wrote Openbox theme to {theme}")
PY
