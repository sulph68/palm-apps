#!/usr/bin/env python3
# mkicon_bitwarden.py - BitWarden launcher icons. Both sources are
# user-supplied, already authored at native PalmOS size (22x22 large,
# 15x9 small - the hard SMALLICON size) with pure black-or-transparent
# pixels (no anti-aliasing) - straight threshold-and-copy into 1-bit BMPs,
# same shape as lazyFTP's own mkicon_lazyftp.py.
import os
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
LARGE_SRC = os.path.join(HERE, "BitWardeniOS_large.png")
SMALL_SRC = os.path.join(HERE, "BitWardeniOS_small.png")


def _lit(im, x, y):
    r, g, b, a = im.getpixel((x, y))
    return a >= 128 and (r + g + b) / 3 < 128


def convert(src, out, w, h):
    ref = Image.open(src).convert("RGBA")
    assert ref.size == (w, h), f"{src} is {ref.size}, expected {(w, h)}"
    im = Image.new("1", (w, h), 1)  # 1 = white bg
    for y in range(h):
        for x in range(w):
            if _lit(ref, x, y):
                im.putpixel((x, y), 0)
    im.save(out, "BMP")
    return im


if __name__ == "__main__":
    lf = convert(LARGE_SRC, os.path.join(HERE, "bitwarden_large.bmp"), 22, 22)
    sf = convert(SMALL_SRC, os.path.join(HERE, "bitwarden_small.bmp"), 15, 9)

    lf.convert("L").resize((22 * 14, 22 * 14), Image.NEAREST).save(
        os.path.join(HERE, "preview_large_1bit_x14.png")
    )
    sf.convert("L").resize((15 * 16, 9 * 16), Image.NEAREST).save(
        os.path.join(HERE, "preview_small_1bit_x16.png")
    )
    print("wrote bitwarden_large.bmp (22x22 1-bit) + bitwarden_small.bmp (15x9 1-bit)")
