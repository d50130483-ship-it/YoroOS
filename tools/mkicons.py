#!/usr/bin/env python3
"""Builds every icon image YoroOS uses. Nothing is drawn in code at run time.

Outputs
  assets/icons/<key>.png          19 app / desktop icons, 512x512 RGBA, transparent background
  assets/icons/glyphs/<key>.png   19 UI glyphs (search, wifi, check, ...), 512x512 RGBA, white

Sources
  assets/icons/src/<key>.png      generated artwork; the tile background is removed here
  assets/icons/vector/*.svg       open-licence vector icons (see assets/icons/NOTICE.md)
  assets/icons/matronix_logo.png  logo from the Matronix Browser repository

Run:  python3 tools/mkicons.py
"""
import os
import sys
from collections import deque

from PIL import Image

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
ICON_DIR = os.path.join(ROOT, "assets", "icons")
GLYPH_DIR = os.path.join(ICON_DIR, "glyphs")
VECTOR_DIR = os.path.join(ICON_DIR, "vector")
SIZE = 512
TOL = 18  # max colour step between neighbouring pixels while flooding the background
TOL_OVERRIDE = {"trash_full": 8, "settings": 8}  # low-contrast artwork needs a tighter fill

# key -> (vector file, colour) for app icons
VECTOR = {
    "chrome":      ("chrome.svg",      "#4285F4"),
    "firefox":     ("firefox.svg",     "#FF7139"),
    "steam":       ("steam.svg",       "#E2E8F0"),
    "python":      ("python.svg",      "#3776AB"),
    "vscode":      ("vscode.svg",      "#0078D4"),
    "exe":         ("exe.svg",         "#7C3AED"),
    "txt":         ("txt.svg",         "#94A3B8"),
    "zip":         ("zip.svg",         "#F59E0B"),
    "trash_empty": ("trash_empty.svg", "#94A3B8"),
}
GENERATED = ["yoro", "files", "folder", "trash_full",
             "browser", "store", "terminal", "settings", "installer"]

# UI glyph -> Lucide icon name (ISC). Rendered white so they read on the dark taskbar,
# accent buttons and tiles.
GLYPH_COLOUR = "#F8FAFC"
GLYPHS = {
    "calc":       "calculator",
    "image":      "image",
    "paint":      "palette",
    "git":        "git-branch",
    "vlc":        "play",
    "discord":    "message-circle",
    "taskmgr":    "activity",
    "regedit":    "database",
    "power":      "power",
    "search":     "search",
    "wifi":       "wifi",
    "volume":     "volume-2",
    "bell":       "bell",
    "up":         "chevron-up",
    "back":       "chevron-left",
    "plus":       "plus",
    "download":   "download",
    "arrow_down": "arrow-down",
    "check":      "check",
}
LUCIDE_DIR = os.environ.get("LUCIDE_DIR", "/tmp/npmget/package/icons")


def step_ok(a, b, tol):
    return max(abs(a[0] - b[0]), abs(a[1] - b[1]), abs(a[2] - b[2])) <= tol


def remove_background(img: Image.Image, tol: int = TOL) -> Image.Image:
    """Flood-fills from the image border; a pixel joins the background when it is
    within tol of the pixel it was reached from. Smooth gradients are followed,
    while the symbol's edges (higher contrast) stop the fill."""
    img = img.convert("RGBA")
    w, h = img.size
    px = img.convert("RGB").load()
    alpha = bytearray(b"\xff") * (w * h)
    seen = bytearray(w * h)
    q = deque()
    for x in range(w):
        for y in (0, h - 1):
            i = y * w + x
            if not seen[i]:
                seen[i] = 1
                q.append((x, y))
    for y in range(h):
        for x in (0, w - 1):
            i = y * w + x
            if not seen[i]:
                seen[i] = 1
                q.append((x, y))
    while q:
        x, y = q.popleft()
        cur = px[x, y]
        alpha[y * w + x] = 0
        for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
            if 0 <= nx < w and 0 <= ny < h:
                i = ny * w + nx
                if not seen[i] and step_ok(cur, px[nx, ny], tol):
                    seen[i] = 1
                    q.append((nx, ny))
    out = img.copy()
    out.putalpha(Image.frombytes("L", (w, h), bytes(alpha)))
    return out


def render_svg(svg_path: str, colour: str, out_path: str) -> None:
    import resvg_py
    with open(svg_path, encoding="utf-8") as fh:
        svg = fh.read()
    svg = svg.replace("currentColor", colour)
    # Simple Icons / Codicons come without a fill or stroke on the root: fill them.
    # Lucide sets fill="none" and strokes, so it keeps its outline style.
    if 'fill="none"' not in svg and 'stroke=' not in svg and "<path " in svg:
        head, tail = svg.split("<path ", 1)
        tag = tail.split(">", 1)[0]
        if "fill=" not in tag and "fill=" not in head:
            svg = head + '<path fill="%s" ' % colour + tail
    svg = svg.replace('stroke-width="2"', 'stroke-width="1.6"')
    data = resvg_py.svg_to_bytes(svg_string=svg, width=SIZE, height=SIZE)
    with open(out_path, "wb") as fh:
        fh.write(bytes(data))


def main() -> int:
    os.makedirs(ICON_DIR, exist_ok=True)
    os.makedirs(GLYPH_DIR, exist_ok=True)
    done = []

    for key in GENERATED:
        src = os.path.join(ICON_DIR, "src", key + ".png")
        img = Image.open(src).convert("RGBA").resize((SIZE, SIZE), Image.Resampling.LANCZOS)
        remove_background(img, TOL_OVERRIDE.get(key, TOL)).save(os.path.join(ICON_DIR, key + ".png"))
        done.append(key)

    for key, (fname, colour) in VECTOR.items():
        tmp = os.path.join(ICON_DIR, key + ".png")
        render_svg(os.path.join(VECTOR_DIR, fname), colour, tmp)
        done.append(key)

    mx = Image.open(os.path.join(ICON_DIR, "matronix_logo.png")).convert("RGBA").resize((SIZE, SIZE), Image.Resampling.LANCZOS)
    if mx.getpixel((0, 0))[3] == 255:
        mx = remove_background(mx)
    mx.save(os.path.join(ICON_DIR, "matronix.png"))
    done.append("matronix")

    glyph_done = []
    for key, lucide in GLYPHS.items():
        svg_src = os.path.join(LUCIDE_DIR, lucide + ".svg")
        out = os.path.join(GLYPH_DIR, key + ".png")
        render_svg(svg_src, GLYPH_COLOUR, out)
        glyph_done.append(key)

    print("icons written:", len(done), "glyphs written:", len(glyph_done))
    return 0


if __name__ == "__main__":
    sys.exit(main())
