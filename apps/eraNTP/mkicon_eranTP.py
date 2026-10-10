#!/usr/bin/env python3
# mkicon_eranTP.py - EraNTP launcher icons. 1-bit, drawn as native-resolution
# pixel art (not drawn big and shrunk - shrinking loses fine detail like the
# "N" diagonal at these pixel counts, confirmed by trying several fonts/
# sizes/thresholds through a supersample+LANCZOS+threshold pipeline first).
import math
import os
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ICONS = os.path.join(HERE, "icons")
os.makedirs(ICONS, exist_ok=True)

# Hand-authored pixel glyphs for the small icon's "NTP" text. 5-wide, no
# inter-letter gap: NTP fills the small icon's full 15px width (3 x 5 = 15)
# exactly, and the T's 1px downstroke lands at local col 2 of its slot
# (cols 5-9) = global col 7, the exact center of a 15-wide icon.
GLYPHS_5X7 = {
    "N": ["11001", "11001", "10101", "10101", "10101", "10011", "10011"],
    # T's bar is inset 1px on each side (not full-width) so it doesn't fuse
    # with N's/P's touching columns at zero letter-spacing.
    "T": ["01110", "00100", "00100", "00100", "00100", "00100", "00100"],
    "P": ["11110", "10010", "10010", "11110", "10000", "10000", "10000"],
}


def _draw_ntp_text(im, glyphs, gw, gh, y0, gap=1):
    letters = "NTP"
    total_w = len(letters) * gw + (len(letters) - 1) * gap
    # Bias the extra pixel to the left so an odd leftover centers the text
    # on im.width // 2 exactly (matches the clock's center column above it).
    x0 = (im.width - total_w + 1) // 2
    for i, ch in enumerate(letters):
        gx = x0 + i * (gw + gap)
        for row, bits in enumerate(glyphs[ch]):
            for col, bit in enumerate(bits):
                if bit == "1":
                    im.putpixel((gx + col, y0 + row), 0)


def draw_large(out, w=22, h=22):
    # 22x22 - the actual size this ROM's launcher renders (every other icon
    # in this suite is 22 tall too); a 32-tall icon gets its bottom rows
    # clipped by the launcher grid cell, which is what was truncating the
    # "NTP" text before. No text here now - just the clock+stand mark, sized
    # to fill the full square since there's no text row budget to leave room
    # for.
    im = Image.new("1", (w, h), 1)  # 1 = white
    d = ImageDraw.Draw(im)

    cx, cy, r = w // 2, 9, 8
    d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=0, width=2, fill=1)

    hour_ang = math.radians(0)  # 3 o'clock
    hx = cx + round(r * 0.5 * math.cos(hour_ang))
    hy = cy + round(r * 0.5 * math.sin(hour_ang))
    d.line([(cx, cy), (hx, hy)], fill=0, width=1)

    min_ang = math.radians(-90)  # 12 o'clock (3:00 exactly)
    mx = cx + round(r * 0.8 * math.cos(min_ang))
    my = cy + round(r * 0.8 * math.sin(min_ang))
    d.line([(cx, cy), (mx, my)], fill=0, width=1)

    d.point((cx, cy), fill=0)

    # --- Stand: strut down from clock to a network bar ---
    bar_y = min(h - 3, cy + r + 2)
    d.line([(cx, cy + r), (cx, bar_y)], fill=0, width=1)
    bar_half = 7
    d.line([(cx - bar_half, bar_y), (cx + bar_half, bar_y)], fill=0, width=1)
    for fx in (cx - bar_half, cx, cx + bar_half):
        d.line([(fx, bar_y), (fx, bar_y + 1)], fill=0, width=1)

    im.save(out, "BMP")
    return im


def draw_small(out, w=15, h=9):
    im = Image.new("1", (w, h), 1)  # 1 = white
    _draw_ntp_text(im, GLYPHS_5X7, gw=5, gh=7, y0=(h - 7) // 2, gap=0)
    im.save(out, "BMP")
    return im


if __name__ == "__main__":
    lf = draw_large(os.path.join(ICONS, "eranTP_large.bmp"))
    sf = draw_small(os.path.join(ICONS, "eranTP_small.bmp"))

    # Nearest-neighbor upscales of the real 1-bit output, for review.
    lf.convert("L").resize((22 * 14, 22 * 14), Image.NEAREST).save(
        os.path.join(ICONS, "preview_large_1bit_x14.png")
    )
    sf.convert("L").resize((15 * 16, 9 * 16), Image.NEAREST).save(
        os.path.join(ICONS, "preview_small_1bit_x16.png")
    )
    print("wrote eranTP_large.bmp (22x22 1-bit) + eranTP_small.bmp (15x9 1-bit)")
    print("wrote preview PNGs for review")
