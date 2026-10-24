#!/usr/bin/env python3
"""Builds the 19 desktop/store icons in assets/icons/<key>.png (512x512 RGBA, transparent background).

Sources:
  assets/icons/src/<key>.png   - generated artwork (flat illustrations on a coloured tile);
                                  the tile background is removed here.
  assets/icons/vector/<key>.svg - open-licence vector icons (see assets/icons/NOTICE.md),
                                  rendered with resvg with the colour set below.
  assets/icons/matronix_logo.png - logo from the Matronix Browser repository (opaque corners
                                  are cleared the same way as generated artwork).

Run:  python3 tools/mkicons.py
"""
import os
import sys
from collections import deque

from PIL import Image

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
ICON_DIR = os.path.join(ROOT, "assets", "icons")
SIZE = 512
TOL = 18  # max colour step between neighbouring pixels while flooding the background
TOL_OVERRIDE = {"trash_full": 8, "settings": 8}  # low-contrast artwork needs a tighter fill

# key -> (vector file, colour)
VECTOR = {
    "chrome":  ("chrome.svg",  "#4285F4"),
    "firefox": ("firefox.svg", "#FF7139"),
    "steam":   ("steam.svg",   "#E2E8F0"),
    "python":  ("python.svg",  "#3776AB"),
    "vscode":  ("vscode.svg",  "#0078D4"),
    "exe":     ("exe.svg",     "#7C3AED"),
    "txt":     ("txt.svg",     "#94A3B8"),
    "zip":     ("zip.svg",     "#F59E0B"),
    "trash_empty": ("trash_empty.svg", "#94A3B8"),
}
GENERATED = ["yoro", "files", "folder", "trash_full",
             "browser", "store", "terminal", "settings", "installer"]


def step_ok(a, b, tol):
    return max(abs(a[0] - b[0]), abs(a[1] - b[1]), abs(a[2] - b[2])) <= tol


def remove_background(img: Image.Image, tol: int = TOL) -> Image.Image:
    """Flood-fills from the image border; a pixel joins the background when it is
    within TOL of the pixel it was reached from. Smooth gradients are followed,
    while the symbol's edges (higher contrast) stop the fill."""
    img = img.convert("RGBA")
    w, h = img.size
    rgb = img.convert("RGB")
    px = rgb.load()
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


def render_vector(path: str, colour: str, key: str) -> Image.Image:
    import resvg_py
    with open(path, encoding="utf-8") as fh:
        svg = fh.read()
    # Icon sets use either no fill (Simple Icons), currentColor (Lucide, Codicons) or
    # stroke="currentColor" (Lucide). Set the colour explicitly and thin Lucide strokes.
    svg = svg.replace("currentColor", colour)
    if "<path " in svg and 'fill="' not in svg.split("<path ", 1)[1].split(">", 1)[0]:
        svg = svg.replace("<path ", '<path fill="%s" ' % colour, 1)
    svg = svg.replace('stroke-width="2"', 'stroke-width="1.6"')
    data = resvg_py.svg_to_bytes(svg_string=svg, width=SIZE, height=SIZE)
    tmp = os.path.join(ICON_DIR, "vector", key + ".rendered.png")
    with open(tmp, "wb") as fh:
        fh.write(bytes(data))
    img = Image.open(tmp).convert("RGBA")
    os.remove(tmp)
    return img


def main() -> int:
    os.makedirs(ICON_DIR, exist_ok=True)
    done = []

    for key in GENERATED:
        src = os.path.join(ICON_DIR, "src", key + ".png")
        img = Image.open(src).convert("RGBA").resize((SIZE, SIZE), Image.Resampling.LANCZOS)
        out = remove_background(img, TOL_OVERRIDE.get(key, TOL))
        out.save(os.path.join(ICON_DIR, key + ".png"))
        done.append(key)

    for key, (fname, colour) in VECTOR.items():
        img = render_vector(os.path.join(ICON_DIR, "vector", fname), colour, key)
        img.save(os.path.join(ICON_DIR, key + ".png"))
        done.append(key)

    mx = Image.open(os.path.join(ICON_DIR, "matronix_logo.png")).convert("RGBA").resize((SIZE, SIZE), Image.Resampling.LANCZOS)
    if mx.getpixel((0, 0))[3] == 255:
        mx = remove_background(mx)
    mx.save(os.path.join(ICON_DIR, "matronix.png"))
    done.append("matronix")

    print("icons written:", len(done), sorted(done))
    return 0


if __name__ == "__main__":
    sys.exit(main())
