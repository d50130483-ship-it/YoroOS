# Генерирует kernel/font.c: растровые шрифты 8x14 (sans + mono) с кириллицей из DejaVu
from PIL import Image, ImageDraw, ImageFont
import os

W, H = 8, 14
SIZE = 14

chars = [chr(c) for c in range(0x20, 0x7F)]
chars += [chr(c) for c in range(0x410, 0x450)]  # А-я
chars += [chr(0x401), chr(0x451)]               # Ё ё
chars += list("«»—–…№°·•→←↑↓✓✕│─┌┐└┘│┬┴┼█▓▒░")
seen = set()
chars = [c for c in chars if not (c in seen or seen.add(c))]

FD = "/usr/share/fonts/truetype/dejavu"


def raster(fontpath):
    font = ImageFont.truetype(fontpath, SIZE)
    out = []
    for ch in chars:
        img = Image.new("L", (W + 12, H + 8), 0)
        dr = ImageDraw.Draw(img)
        dr.text((6, 0), ch, fill=255, font=font)
        bbox = img.getbbox()
        if bbox is None:
            out.append([0] * H)
            continue
        gw, gh = bbox[2] - bbox[0], bbox[3] - bbox[1]
        crop = img.crop(bbox)
        if gw > W:
            crop = crop.resize((W, max(1, int(gh * W / gw))))
        if crop.height > H:
            crop = crop.resize((max(1, int(crop.width * H / crop.height)), H))
        glyph = [0] * H
        ox = (W - crop.width) // 2
        oy = (H - crop.height) // 2 + (0 if gh < 12 else 0)
        px = crop.load()
        for y in range(crop.height):
            row = 0
            for x in range(crop.width):
                if px[x, y] > 110:
                    row |= 1 << (7 - (ox + x))
            if 0 <= oy + y < H:
                glyph[oy + y] = row
        out.append(glyph)
    return out


def emit(name, data, f):
    f.write(f"const unsigned char font_{name}[{len(data)}][{H}] = {{\n")
    for g in data:
        f.write("{" + ",".join(f"0x{b:02x}" for b in g) + "},\n")
    f.write("};\n\n")


# Частоты MIDI -> Гц (целые) для PC-динамика
hz = [round(440 * 2 ** ((m - 69) / 12)) for m in range(128)]

with open(os.path.join(os.path.dirname(__file__), "..", "kernel", "font.c"), "w") as f:
    f.write("// Сгенерировано scripts/genfont.py — не редактировать вручную\n")
    f.write("#include \"font.h\"\n\n")
    f.write(f"const unsigned int font_count = {len(chars)};\n")
    f.write("const unsigned int font_cps[] = {")
    f.write(",".join(str(ord(c)) for c in chars))
    f.write("};\n\n")
    f.write("const unsigned short midi_hz[128] = {\n")
    f.write(",".join(str(x) for x in hz) + "\n};\n\n")
    emit("sans", raster(f"{FD}/DejaVuSans.ttf"), f)
    emit("mono", raster(f"{FD}/DejaVuSansMono.ttf"), f)
print(f"font.c: {len(chars)} глифов, 8x14")
