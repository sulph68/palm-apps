#!/usr/bin/env python3
# mkicon_types.py - per-item-type row icons (Login/Note/Card/Identity), shown
# in MainForm.c's list rows. User-supplied PNGs, already authored at native
# size (11x7 - matches the list row's own FntLineHeight()) with pure
# black-or-transparent pixels (no anti-aliasing) - same straight
# threshold-and-copy as mkicon_bitwarden.py's own launcher icons.
import os
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
TYPES = ["Login", "Note", "Card", "Identity"]


def _lit(im, x, y):
    r, g, b, a = im.getpixel((x, y))
    return a >= 128 and (r + g + b) / 3 < 128


def convert(name):
    src = os.path.join(HERE, f"{name}.png")
    ref = Image.open(src).convert("RGBA")
    w, h = ref.size
    im = Image.new("1", (w, h), 1)  # 1 = white bg
    for y in range(h):
        for x in range(w):
            if _lit(ref, x, y):
                im.putpixel((x, y), 0)
    out = os.path.join(HERE, f"{name.lower()}_row.bmp")
    im.save(out, "BMP")
    im.convert("L").resize((w * 20, h * 20), Image.NEAREST).save(
        os.path.join(HERE, f"preview_{name.lower()}_row_x20.png")
    )
    return w, h


if __name__ == "__main__":
    for t in TYPES:
        w, h = convert(t)
        print(f"wrote {t.lower()}_row.bmp ({w}x{h} 1-bit)")
