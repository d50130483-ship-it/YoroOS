#!/usr/bin/env python3
"""
Generate embedded compressed wallpapers (from real generated PNGs in assets/)
and crisp 12x-supersampled minimalist flat RGBA icons (48x48, 32x32, 20x20)
with ZERO Windows logos and ZERO jagged pixels.
Also outputs build/desktop_preview.png so we can visually inspect 1:1 quality!
"""
import math
import os
import struct
from PIL import Image, ImageDraw, ImageFilter, ImageFont

os.makedirs("build", exist_ok=True)

def qoi_encode_rgb(img: Image.Image) -> bytes:
    """Standard QOI lossless encoder (RGB, sRGB) — decodes in 40 lines of C!"""
    img = img.convert("RGB")
    w, h = img.size
    raw = img.tobytes()
    out = bytearray()
    out += b"qoif" + struct.pack(">IIBB", w, h, 3, 0)
    index = [(0, 0, 0, 255)] * 64
    pr, pg, pb, pa = 0, 0, 0, 255
    run = 0
    n = len(raw)
    for off in range(0, n, 3):
        r, g, b = raw[off], raw[off + 1], raw[off + 2]
        if r == pr and g == pg and b == pb:
            run += 1
            if run == 62 or off + 3 == n:
                out.append(0xC0 | (run - 1))
                run = 0
        else:
            if run > 0:
                out.append(0xC0 | (run - 1))
                run = 0
            idx = (r * 3 + g * 5 + b * 7 + 255 * 11) & 63
            if index[idx] == (r, g, b, 255):
                out.append(idx)
            else:
                index[idx] = (r, g, b, 255)
                dr = ((r - pr) + 128) % 256 - 128
                dg = ((g - pg) + 128) % 256 - 128
                db = ((b - pb) + 128) % 256 - 128
                dg_r = dr - dg
                dg_b = db - dg
                if -2 <= dr <= 1 and -2 <= dg <= 1 and -2 <= db <= 1:
                    out.append(0x40 | ((dr + 2) << 4) | ((dg + 2) << 2) | (db + 2))
                elif -32 <= dg <= 31 and -8 <= dg_r <= 7 and -8 <= dg_b <= 7:
                    out.append(0x80 | (dg + 32))
                    out.append(((dg_r + 8) << 4) | (dg_b + 8))
                else:
                    out.append(0xFE)
                    out.extend((r, g, b))
            pr, pg, pb = r, g, b
    out += b"\x00\x00\x00\x00\x00\x00\x00\x01"
    return bytes(out)

# Quantize smooth gradients gently (e.g. 6-bit per channel + tiny smoothing) so QOI achieves ~55 KB per wallpaper!
def prep_wallpaper(path: str, w: int = 512, h: int = 384) -> tuple[bytes, Image.Image]:
    im = Image.open(path).convert("RGB").resize((w, h), Image.Resampling.LANCZOS)
    # Slight smooth blur removes PNG grain so runs compress tightly & upscale silky smooth
    im = im.filter(ImageFilter.GaussianBlur(radius=0.6))
    px = bytearray(im.tobytes())
    # Keep 7-bit precision (step 2) -> indistinguishable visually + tiny QOI size!
    for i in range(len(px)):
        px[i] = px[i] & 0xFE
    im_q = Image.frombytes("RGB", (w, h), bytes(px))
    return qoi_encode_rgb(im_q), im_q

wp_files = [
    ("assets/wp_0_nordic.png", "Nordic Twilight"),
    ("assets/wp_1_dunes.png", "Warm Dunes"),
    ("assets/wp_2_emerald.png", "Emerald Mist"),
]

wp_qois = []
wp_previews = []
for p, name in wp_files:
    blob, im_q = prep_wallpaper(p, 512, 384)
    wp_qois.append((name, blob))
    wp_previews.append(im_q)
    print(f"Wallpaper '{name}': {len(blob):,} bytes QOI")

# ---------------------------------------------------------------------------
# 100% CLEAN, MINIMALIST FLAT VECTOR ICON GENERATOR (512x512 -> LANCZOS RGBA)
# ZERO Windows 4-squares! Every icon is simple, bold, calm, anti-aliased!
# ---------------------------------------------------------------------------
S = 512

def vgrad_squircle(c1: tuple[int, int, int], c2: tuple[int, int, int], pad: int = 44, radius: int = 112) -> Image.Image:
    base = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    # Subtle soft drop shadow
    sh = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    sd = ImageDraw.Draw(sh)
    sd.rounded_rectangle([pad, pad + 14, S - pad, S - pad + 14], radius=radius, fill=(0, 0, 0, 95))
    sh = sh.filter(ImageFilter.GaussianBlur(radius=16))
    base.alpha_composite(sh)

    grad = Image.new("RGBA", (S, S), (0, 0, 0, 255))
    gp = bytearray(S * S * 4)
    span = max(1, S - 2 * pad)
    for y in range(S):
        t = max(0.0, min(1.0, (y - pad) / span))
        r = int(c1[0] + (c2[0] - c1[0]) * t)
        g = int(c1[1] + (c2[1] - c1[1]) * t)
        b = int(c1[2] + (c2[2] - c1[2]) * t)
        row = bytes([r, g, b, 255]) * S
        gp[y * S * 4:(y + 1) * S * 4] = row
    grad = Image.frombytes("RGBA", (S, S), bytes(gp))

    mask = Image.new("L", (S, S), 0)
    md = ImageDraw.Draw(mask)
    md.rounded_rectangle([pad, pad, S - pad, S - pad], radius=radius, fill=255)
    tile = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    tile.paste(grad, (0, 0), mask)

    # Subtle 1px inner top highlight
    hl = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    hd = ImageDraw.Draw(hl)
    hd.rounded_rectangle([pad + 3, pad + 3, S - pad - 3, S - pad - 3], radius=radius - 3, outline=(255, 255, 255, 42), width=6)
    tile.alpha_composite(hl)
    base.alpha_composite(tile)
    return base

def thick_line(d: ImageDraw.ImageDraw, p1, p2, w: int, col):
    d.line([p1, p2], fill=col, width=w)
    r = w / 2.0
    for x, y in (p1, p2):
        d.ellipse([x - r, y - r, x + r, y + r], fill=col)

def icon_yoro() -> Image.Image:
    # Original YoroOS identity mark: calm sky-indigo circle with clean white geometric 'Y' + orbit dot
    im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    sh = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    ImageDraw.Draw(sh).ellipse([48, 60, S - 48, S - 36], fill=(0, 0, 0, 90))
    im.alpha_composite(sh.filter(ImageFilter.GaussianBlur(16)))

    mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(mask).ellipse([48, 48, S - 48, S - 48], fill=255)
    grad = vgrad_squircle((56, 189, 248), (79, 70, 229), pad=30, radius=220)
    circ = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    circ.paste(grad, (0, 0), mask)
    d = ImageDraw.Draw(circ)
    d.ellipse([52, 52, S - 52, S - 52], outline=(255, 255, 255, 55), width=7)
    w = (255, 255, 255, 255)
    # Clean balanced minimalist Y mark
    thick_line(d, (168, 168), (256, 266), 40, w)
    thick_line(d, (344, 168), (256, 266), 40, w)
    thick_line(d, (256, 266), (256, 366), 40, w)
    im.alpha_composite(circ)
    return im

def icon_folder() -> Image.Image:
    im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    sh = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    ImageDraw.Draw(sh).rounded_rectangle([46, 118, S - 46, S - 62], radius=54, fill=(0, 0, 0, 95))
    im.alpha_composite(sh.filter(ImageFilter.GaussianBlur(16)))
    d = ImageDraw.Draw(im)
    # Back tab (clean soft ocean-blue minimal folder — NOT Windows bright yellow!)
    d.rounded_rectangle([48, 92, 246, 210], radius=38, fill=(56, 152, 236, 255))
    d.rounded_rectangle([48, 132, S - 48, S - 72], radius=50, fill=(43, 128, 214, 255))
    # Subtle paper card peeking inside
    d.rounded_rectangle([84, 146, S - 84, 230], radius=24, fill=(241, 248, 255, 245))
    # Clean front pocket
    front = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    fd = ImageDraw.Draw(front)
    fd.rounded_rectangle([48, 178, S - 48, S - 72], radius=50, fill=(96, 179, 251, 255))
    fd.rounded_rectangle([51, 181, S - 51, S - 75], radius=47, outline=(255, 255, 255, 65), width=6)
    im.alpha_composite(front)
    return im

def icon_files_app() -> Image.Image:
    # Distinct square app tile for File Manager
    im = vgrad_squircle((59, 130, 246), (29, 78, 216))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([126, 150, 250, 214], radius=24, fill=(191, 219, 254, 245))
    d.rounded_rectangle([126, 184, 386, 366], radius=34, fill=(255, 255, 255, 250))
    return im

def icon_trash(full: bool) -> Image.Image:
    im = vgrad_squircle((71, 85, 105), (41, 50, 65))
    d = ImageDraw.Draw(im)
    if full:
        # Bright angled papers sticking clearly above the rim when Trash has files
        d.rounded_rectangle([168, 96, 286, 224], radius=18, fill=(147, 197, 253, 255))
        d.rounded_rectangle([224, 88, 344, 224], radius=18, fill=(255, 255, 255, 255))
        thick_line(d, (248, 122), (318, 122), 14, (100, 116, 139, 220))
        thick_line(d, (248, 150), (308, 150), 14, (100, 116, 139, 220))
    else:
        d.rounded_rectangle([216, 136, 296, 172], radius=14, outline=(255, 255, 255, 245), width=18)
    thick_line(d, (148, 176), (364, 176), 24, (255, 255, 255, 255))
    # Clean rounded bin body
    d.rounded_rectangle([168, 200, 344, 382], radius=32, fill=(241, 245, 249, 250))
    for x in (224, 288):
        thick_line(d, (x, 238), (x, 342), 20, (51, 65, 85, 220))
    if full:
        d.ellipse([326, 92, 410, 176], fill=(56, 189, 248, 255))
    return im

def icon_browser() -> Image.Image:
    im = vgrad_squircle((14, 165, 233), (37, 99, 235))
    d = ImageDraw.Draw(im)
    w = (255, 255, 255, 250)
    d.ellipse([128, 128, 384, 384], outline=w, width=24)
    d.ellipse([196, 128, 316, 384], outline=w, width=20)
    thick_line(d, (136, 256), (376, 256), 20, w)
    return im

def icon_store() -> Image.Image:
    # Clean minimal App Bag (ZERO Windows squares!)
    im = vgrad_squircle((139, 92, 246), (99, 102, 241))
    d = ImageDraw.Draw(im)
    d.arc([196, 122, 316, 242], start=180, end=360, fill=(255, 255, 255, 250), width=26)
    d.rounded_rectangle([144, 184, 368, 376], radius=40, fill=(255, 255, 255, 250))
    # Subtle minimal downward chevron / plus inside bag
    thick_line(d, (256, 240), (256, 320), 24, (99, 102, 241, 255))
    thick_line(d, (222, 288), (256, 322), 24, (99, 102, 241, 255))
    thick_line(d, (290, 288), (256, 322), 24, (99, 102, 241, 255))
    return im

def icon_terminal() -> Image.Image:
    im = vgrad_squircle((39, 39, 42), (20, 20, 23))
    d = ImageDraw.Draw(im)
    thick_line(d, (148, 186), (226, 252), 30, (74, 222, 128, 255))
    thick_line(d, (226, 252), (148, 318), 30, (74, 222, 128, 255))
    thick_line(d, (256, 318), (356, 318), 30, (244, 244, 245, 255))
    return im

def icon_settings() -> Image.Image:
    im = vgrad_squircle((100, 116, 139), (51, 65, 85))
    d = ImageDraw.Draw(im)
    w = (248, 250, 252, 255)
    for i in range(8):
        ang = i * math.pi / 4.0
        x1 = 256 + math.cos(ang) * 96
        y1 = 256 + math.sin(ang) * 96
        x2 = 256 + math.cos(ang) * 134
        y2 = 256 + math.sin(ang) * 134
        thick_line(d, (x1, y1), (x2, y2), 40, w)
    d.ellipse([156, 156, 356, 356], fill=w)
    d.ellipse([208, 208, 304, 304], fill=(61, 75, 95, 255))
    return im

def icon_installer() -> Image.Image:
    im = vgrad_squircle((16, 185, 129), (5, 150, 105))
    d = ImageDraw.Draw(im)
    w = (255, 255, 255, 250)
    # Clean arrow down into sleek SSD bar
    thick_line(d, (256, 134), (256, 272), 32, w)
    thick_line(d, (200, 222), (256, 278), 32, w)
    thick_line(d, (312, 222), (256, 278), 32, w)
    d.rounded_rectangle([140, 312, 372, 376], radius=24, fill=w)
    d.ellipse([322, 332, 346, 356], fill=(5, 150, 105, 255))
    return im

def icon_exe() -> Image.Image:
    # ZERO Windows squares! Clean app window + play triangle
    im = vgrad_squircle((99, 102, 241), (67, 56, 202))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([120, 140, 392, 372], radius=34, fill=(255, 255, 255, 248))
    d.rounded_rectangle([120, 140, 392, 198], radius=28, fill=(199, 210, 254, 255))
    d.polygon([(228, 232), (228, 330), (314, 281)], fill=(79, 70, 229, 255))
    return im

def icon_txt() -> Image.Image:
    im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    sh = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    ImageDraw.Draw(sh).rounded_rectangle([96, 62, 416, 464], radius=44, fill=(0, 0, 0, 95))
    im.alpha_composite(sh.filter(ImageFilter.GaussianBlur(16)))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([96, 52, 416, 452], radius=42, fill=(248, 250, 252, 255))
    d.rounded_rectangle([99, 55, 413, 449], radius=39, outline=(203, 213, 225, 255), width=6)
    for idx, (y, x2) in enumerate([(168, 350), (234, 350), (300, 350), (366, 276)]):
        thick_line(d, (158, y), (x2, y), 24, (100, 116, 139, 220) if idx else (59, 130, 246, 240))
    return im

def icon_zip() -> Image.Image:
    im = vgrad_squircle((245, 158, 11), (180, 83, 9))
    d = ImageDraw.Draw(im)
    for y in range(96, 260, 40):
        xoff = -14 if ((y // 40) % 2 == 0) else 14
        d.rounded_rectangle([236 + xoff, y, 276 + xoff, y + 26], radius=8, fill=(255, 255, 255, 240))
    d.rounded_rectangle([216, 274, 296, 368], radius=24, fill=(255, 255, 255, 250))
    d.ellipse([240, 314, 272, 346], fill=(180, 83, 9, 255))
    return im

def icon_matronix() -> Image.Image:
    im = vgrad_squircle((24, 24, 28), (9, 9, 11))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([47, 47, S - 47, S - 47], radius=109, outline=(148, 163, 184, 90), width=7)
    w = (255, 255, 255, 255)
    thick_line(d, (152, 350), (152, 162), 34, w)
    thick_line(d, (152, 162), (256, 280), 34, w)
    thick_line(d, (256, 280), (360, 162), 34, w)
    thick_line(d, (360, 162), (360, 350), 34, w)
    return im

def icon_chrome() -> Image.Image:
    im = vgrad_squircle((248, 250, 252), (226, 232, 240))
    d = ImageDraw.Draw(im)
    d.pieslice([104, 104, 408, 408], 210, 330, fill=(239, 68, 68, 255))
    d.pieslice([104, 104, 408, 408], 330, 90, fill=(250, 204, 21, 255))
    d.pieslice([104, 104, 408, 408], 90, 210, fill=(34, 197, 94, 255))
    d.ellipse([182, 182, 330, 330], fill=(255, 255, 255, 255))
    d.ellipse([198, 198, 314, 314], fill=(59, 130, 246, 255))
    return im

def icon_firefox() -> Image.Image:
    im = vgrad_squircle((124, 58, 237), (67, 56, 202))
    d = ImageDraw.Draw(im)
    d.ellipse([126, 126, 386, 386], fill=(249, 115, 22, 255))
    d.ellipse([172, 152, 356, 336], fill=(250, 204, 21, 255))
    d.ellipse([186, 178, 336, 328], fill=(99, 102, 241, 255))
    return im

def icon_steam() -> Image.Image:
    im = vgrad_squircle((30, 58, 138), (15, 23, 42))
    d = ImageDraw.Draw(im)
    w = (241, 245, 249, 255)
    thick_line(d, (166, 326), (318, 204), 44, w)
    d.ellipse([262, 148, 374, 260], fill=w)
    d.ellipse([288, 174, 348, 234], fill=(23, 37, 84, 255))
    d.ellipse([122, 282, 210, 370], fill=w)
    return im

def icon_python() -> Image.Image:
    im = vgrad_squircle((30, 41, 59), (15, 23, 42))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([196, 120, 320, 266], radius=46, fill=(56, 189, 248, 255))
    d.rounded_rectangle([136, 182, 312, 266], radius=42, fill=(56, 189, 248, 255))
    d.rounded_rectangle([192, 246, 316, 392], radius=46, fill=(250, 204, 21, 255))
    d.rounded_rectangle([200, 246, 376, 330], radius=42, fill=(250, 204, 21, 255))
    d.ellipse([218, 144, 242, 168], fill=(255, 255, 255, 255))
    d.ellipse([270, 344, 294, 368], fill=(15, 23, 42, 255))
    return im

def icon_vscode() -> Image.Image:
    im = vgrad_squircle((14, 165, 233), (2, 132, 199))
    d = ImageDraw.Draw(im)
    w = (255, 255, 255, 250)
    thick_line(d, (338, 148), (338, 364), 44, w)
    thick_line(d, (164, 326), (338, 156), 38, w)
    thick_line(d, (164, 186), (338, 356), 38, w)
    return im


ICON_DIR = os.path.join("assets", "icons")

def gen_icon(key: str, fallback) -> Image.Image:
    """Use the generated artwork assets/icons/<key>.png when present, otherwise the
    procedural fallback. Artwork is cropped to the same rounded-square tile."""
    path = os.path.join(ICON_DIR, key + ".png")
    if not os.path.exists(path):
        return fallback()
    src = Image.open(path).convert("RGBA").resize((S, S), Image.Resampling.LANCZOS)
    if src.getchannel("A").getextrema()[0] < 255:
        return src  # already a transparent cut-out (tools/mkicons.py)
    mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, S - 1, S - 1], radius=112, fill=255)
    out = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    out.paste(src, (0, 0), mask)
    return out

ICONS = [
    ("ICON_YORO", gen_icon("yoro", lambda: icon_yoro())),
    ("ICON_FILES", gen_icon("files", lambda: icon_files_app())),
    ("ICON_FOLDER", gen_icon("folder", lambda: icon_folder())),
    ("ICON_TRASH_EMPTY", gen_icon("trash_empty", lambda: icon_trash(False))),
    ("ICON_TRASH_FULL", gen_icon("trash_full", lambda: icon_trash(True))),
    ("ICON_BROWSER", gen_icon("browser", lambda: icon_browser())),
    ("ICON_STORE", gen_icon("store", lambda: icon_store())),
    ("ICON_TERMINAL", gen_icon("terminal", lambda: icon_terminal())),
    ("ICON_SETTINGS", gen_icon("settings", lambda: icon_settings())),
    ("ICON_INSTALLER", gen_icon("installer", lambda: icon_installer())),
    ("ICON_EXE", gen_icon("exe", lambda: icon_exe())),
    ("ICON_TXT", gen_icon("txt", lambda: icon_txt())),
    ("ICON_ZIP", gen_icon("zip", lambda: icon_zip())),
    ("ICON_MATRONIX", gen_icon("matronix", lambda: icon_matronix())),
    ("ICON_CHROME", gen_icon("chrome", lambda: icon_chrome())),
    ("ICON_FIREFOX", gen_icon("firefox", lambda: icon_firefox())),
    ("ICON_STEAM", gen_icon("steam", lambda: icon_steam())),
    ("ICON_PYTHON", gen_icon("python", lambda: icon_python())),
    ("ICON_VSCODE", gen_icon("vscode", lambda: icon_vscode())),
]

# Render a real 1024x768 1:1 visual preview sheet showing desktop icons + dock + all 19 icons!
sheet = wp_previews[0].resize((1024, 768), Image.Resampling.BILINEAR).convert("RGBA")
desk_items = [
    (1, "Файлы"),
    (3, "Корзина"),
    (5, "Браузер"),
    (6, "Приложения"),
    (7, "Терминал"),
    (8, "Настройки"),
    (9, "Установщик"),
    (10, "hello.exe"),
    (12, "Apps.zip"),
    (11, "README.txt"),
]
try:
    font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 12)
    font_b = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 13)
except Exception:
    font = ImageFont.load_default()
    font_b = font

sd = ImageDraw.Draw(sheet)
for idx, (ic_i, label) in enumerate(desk_items):
    col = idx // 6
    row = idx % 6
    x = 24 + col * 96
    y = 22 + row * 92
    ic48 = ICONS[ic_i][1].resize((48, 48), Image.Resampling.LANCZOS)
    sheet.alpha_composite(ic48, (x + 16, y))
    tw = sd.textlength(label, font=font)
    sd.text((x + 40 - tw / 2 + 1, y + 53), label, font=font, fill=(0, 0, 0, 210))
    sd.text((x + 40 - tw / 2, y + 52), label, font=font, fill=(248, 250, 252, 255))

# Showcase card on right showing ALL 19 icons at exact 48x48 and 24x24 sizes
card = Image.new("RGBA", (620, 430), (0, 0, 0, 0))
cd = ImageDraw.Draw(card)
cd.rounded_rectangle([0, 0, 619, 429], radius=16, fill=(17, 24, 39, 228), outline=(255, 255, 255, 34), width=1)
cd.text((22, 16), "YoroOS 0.2.8 — Clean Original Icon Pack (1:1 48px + 24px)", font=font_b, fill=(248, 250, 252, 255))
for i, (nm, im) in enumerate(ICONS):
    c = i % 5
    r = i // 5
    cx = 24 + c * 118
    cy = 56 + r * 92
    card.alpha_composite(im.resize((48, 48), Image.Resampling.LANCZOS), (cx, cy))
    card.alpha_composite(im.resize((24, 24), Image.Resampling.LANCZOS), (cx + 56, cy + 24))
    cd.text((cx, cy + 54), nm.replace("ICON_", ""), font=font, fill=(203, 213, 225, 255))
sheet.alpha_composite(card, (340, 60))

# Modern clean dock/bar preview at bottom (ZERO Windows logo!)
bar = Image.new("RGBA", (1024, 48), (15, 23, 42, 224))
bd = ImageDraw.Draw(bar)
bd.line([(0, 0), (1024, 0)], fill=(255, 255, 255, 28), width=1)
bar.alpha_composite(ICONS[0][1].resize((28, 28), Image.Resampling.LANCZOS), (14, 10))
for j, ic_i in enumerate([1, 5, 6, 7, 8, 9]):
    bar.alpha_composite(ICONS[ic_i][1].resize((28, 28), Image.Resampling.LANCZOS), (236 + j * 44, 10))
sheet.alpha_composite(bar, (0, 720))
sheet.save("build/desktop_preview.png")

# UI glyphs: assets/icons/glyphs/<key>.png (Lucide, ISC licence, white on transparent).
# They follow the app icons in the sprite table; kernel code tints them at draw time.
GLYPH_KEYS = ["calc", "image", "paint", "git", "vlc", "discord", "taskmgr", "regedit",
              "power", "search", "wifi", "volume", "bell", "up", "back", "plus",
              "download", "arrow_down", "check"]
GLYPHS = []
for _k in GLYPH_KEYS:
    _im = Image.open(os.path.join(ICON_DIR, "glyphs", _k + ".png")).convert("RGBA")
    GLYPHS.append(("SPR_G_" + _k.upper(), _im.resize((S, S), Image.Resampling.LANCZOS)))
ALL_SPRITES = ICONS + GLYPHS
SPRITE_COUNT = len(ALL_SPRITES)

# Emit kernel/assets_data.h: sprite count + glyph sprite indices (kernel/icons.c includes it)
with open("kernel/assets_data.h", "w") as h:
    h.write("/* Auto-generated by tools/mkassets.py - do not edit. */\n")
    h.write("#ifndef YORO_ASSETS_DATA_H\n#define YORO_ASSETS_DATA_H\n\n")
    h.write(f"#define ICON_SPRITE_COUNT {SPRITE_COUNT}\n\n")
    h.write("enum {\n")
    for _i, (_nm, _im) in enumerate(ALL_SPRITES):
        if _nm.startswith("SPR_G_"):
            h.write(f"    {_nm} = {_i},\n")
    h.write("};\n\n#endif\n")

# Emit C source kernel/assets_data.c with all 3 QOI wallpapers + pre-supersampled 48x48 & 28x28 BGRA sprites!
def img_to_bgra_c(im: Image.Image, sz: int) -> bytes:
    sm = im.resize((sz, sz), Image.Resampling.LANCZOS).convert("RGBA")
    raw = sm.tobytes()
    out = bytearray(sz * sz * 4)
    for o in range(0, len(raw), 4):
        r, g, b, a = raw[o], raw[o + 1], raw[o + 2], raw[o + 3]
        out[o:o + 4] = bytes((b, g, r, a))
    return bytes(out)

with open("kernel/assets_data.c", "w") as f:
    f.write("/* Auto-generated by tools/mkassets.py — clean wallpapers & supersampled RGBA sprites */\n")
    f.write('#include "kernel.h"\n#include "assets_data.h"\n\n')
    for idx, (name, blob) in enumerate(wp_qois):
        f.write(f"/* Wallpaper {idx}: {name} ({len(blob)} bytes QOI 512x384) */\n")
        f.write(f"const u8 g_wp_qoi_{idx}[{len(blob)}] = {{\n")
        for off in range(0, len(blob), 24):
            chunk = ", ".join(f"0x{b:02x}" for b in blob[off:off + 24])
            f.write(f"    {chunk},\n")
        f.write("};\n")
        f.write(f"const u32 g_wp_qoi_{idx}_len = {len(blob)}U;\n\n")

    f.write(f"const u32 g_icon48_bgra[{SPRITE_COUNT}][48 * 48] = {{\n")
    for nm, im in ALL_SPRITES:
        bgra = img_to_bgra_c(im, 48)
        words = struct.unpack(f"<{48*48}I", bgra)
        f.write(f"    /* {nm} */ {{\n")
        for off in range(0, len(words), 16):
            chunk = ", ".join(f"0x{w:08x}u" for w in words[off:off + 16])
            f.write(f"        {chunk},\n")
        f.write("    },\n")
    f.write("};\n\n")

    f.write(f"const u32 g_icon28_bgra[{SPRITE_COUNT}][28 * 28] = {{\n")
    for nm, im in ALL_SPRITES:
        bgra = img_to_bgra_c(im, 28)
        words = struct.unpack(f"<{28*28}I", bgra)
        f.write(f"    /* {nm} */ {{\n")
        for off in range(0, len(words), 16):
            chunk = ", ".join(f"0x{w:08x}u" for w in words[off:off + 16])
            f.write(f"        {chunk},\n")
        f.write("    },\n")
    f.write("};\n")

print("Generated kernel/assets_data.c successfully! Size:", os.path.getsize("kernel/assets_data.c"), "bytes")
