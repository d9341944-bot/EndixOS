#!/usr/bin/env python3
"""Генерирует gui/font_data.c: растровые глифы с 8-битным альфа-каналом.

Шрифты (оба свободные):
  - Inter (SIL OFL)               -> интерфейс (обычный и полужирный)
  - DejaVu Sans Mono (Bitstream)  -> терминал
Набор: ASCII 32..126, кириллица А-я, Ё, ё.

Запуск: python3 tools/genfont.py > gui/font_data.c
"""
import sys
from PIL import Image, ImageDraw, ImageFont

STYLES = [
    ("regular", "/usr/share/fonts/opentype/inter/Inter-Regular.otf", 13),
    ("bold",    "/usr/share/fonts/opentype/inter/Inter-SemiBold.otf", 13),
    ("mono",    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", 12),
    ("title",   "/usr/share/fonts/opentype/inter/Inter-Bold.otf", 26),
]

CODEPOINTS = (
    list(range(0x20, 0x7F))        # 95 глифов
    + list(range(0x410, 0x450))    # 64 глифа: А..я
    + [0x401, 0x451]               # Ё, ё
)
assert len(CODEPOINTS) == 161

CANVAS_W, CANVAS_H = 48, 48
ORIGIN_X, BASELINE_Y = 12, 32


def render_style(path, size):
    font = ImageFont.truetype(path, size)
    ascent, descent = font.getmetrics()
    glyphs, blob = [], bytearray()
    mono_adv = None
    for cp in CODEPOINTS:
        ch = chr(cp)
        img = Image.new("L", (CANVAS_W, CANVAS_H), 0)
        ImageDraw.Draw(img).text((ORIGIN_X, BASELINE_Y), ch, font=font,
                                 fill=255, anchor="ls")
        adv = int(round(font.getlength(ch)))
        bbox = img.getbbox()
        if bbox is None:
            glyphs.append((len(blob), 0, 0, 0, 0, adv))
            continue
        crop = img.crop(bbox)
        w, h = crop.size
        off = len(blob)
        blob.extend(crop.tobytes())
        glyphs.append((off, w, h, bbox[0] - ORIGIN_X, bbox[1] - BASELINE_Y, adv))
    return glyphs, blob, ascent, ascent + descent


def main():
    out = []
    w = out.append
    w("/* Файл сгенерирован tools/genfont.py, руками не править. */")
    w('#include "font.h"')
    w("")

    all_glyphs, all_blob, ascents, heights = [], bytearray(), [], []
    for name, path, size in STYLES:
        glyphs, blob, asc, hgt = render_style(path, size)
        base = len(all_blob)
        all_glyphs.append([(o + base, *rest) for (o, *rest) in glyphs])
        all_blob.extend(blob)
        ascents.append(asc)
        heights.append(hgt)
        print(f"{name}: {len(glyphs)} glyphs, ascent {asc}, height {hgt}",
              file=sys.stderr)

    w(f"const u8 font_alpha[{len(all_blob)}] = {{")
    for i in range(0, len(all_blob), 24):
        w("    " + ",".join(str(b) for b in all_blob[i:i + 24]) + ",")
    w("};")
    w("")
    w("const font_glyph_t font_glyphs[FONT_STYLES][FONT_NGLYPHS] = {")
    for glyphs in all_glyphs:
        w("    {")
        for (off, gw, gh, gx, gy, adv) in glyphs:
            w(f"        {{{off},{gw},{gh},{gx},{gy},{adv}}},")
        w("    },")
    w("};")
    w("")
    w("const u8 font_ascent[FONT_STYLES] = {" + ",".join(map(str, ascents)) + "};")
    w("const u8 font_height[FONT_STYLES] = {" + ",".join(map(str, heights)) + "};")
    print("\n".join(out))


main()
