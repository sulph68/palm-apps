#!/usr/bin/env python3
# mkicon_lazyftp.py - lazyFTP launcher icons. LARGE (22x22) is a direct,
# pixel-exact reproduction of the user-supplied reference (../large_icon.png,
# already authored at native 22x22 resolution) - no redrawing/reinterpreting,
# just threshold-and-copy. SMALL (15x9) reuses the EXACT "FTP" wordmark
# pixels cropped straight out of that same reference (cols 3-18, rows 13-18
# = the wordmark's own real bounding box, 16x6 - one column over the hard
# 15px PalmOS SMALLICON width). The dropped column matters: a first attempt
# dropped col 18 (2px accent on P's loop, rows 14-15) since it looked like
# the least essential pixel in isolation - but rendered, P lost its only
# distinguishing feature from F entirely (read as "FTF"). Dropped col 6
# instead - the tip of F's top bar, lit on exactly ONE of the six rows - a
# true no-op crop (verified via the reference: col 6 is [True,F,F,F,F,F]
# across rows 13-18, vs col 18's [F,True,True,F,F,F] which is load-bearing
# for P). Not a separately hand-authored font, per the user's explicit ask
# to reuse the large icon's own letterforms as-is.
SKIP_SRC_COL = 6  # F's top-bar tip - the only 1-row-only column in the wordmark
import os
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
LARGE = os.path.join(HERE, "large_caldav.png")
SMALL = os.path.join(HERE, "small_caldav.png")


def _lit(im, x, y):
    r, g, b, a = im.getpixel((x, y))
    return a >= 128 and (r + g + b) / 3 < 128


def draw_large(out, w=22, h=22):
    ref = Image.open(LARGE).convert("RGBA")
    assert ref.size == (w, h), f"reference is {ref.size}, expected {(w, h)}"
    im = Image.new("1", (w, h), 1)  # 1 = white bg
    for y in range(h):
        for x in range(w):
            if _lit(ref, x, y):
                im.putpixel((x, y), 0)
    im.save(out, "BMP")
    return im


def draw_small(out, w=15, h=9):
    ref = Image.open(SMALL).convert("RGBA")
    assert ref.size == (w, h), f"reference is {ref.size}, expected {(w, h)}"
    im = Image.new("1", (w, h), 1)  # 1 = white bg
    for y in range(h):
        for x in range(w):
            if _lit(ref, x, y):
                im.putpixel((x, y), 0)
    im.save(out, "BMP")
    return im


if __name__ == "__main__":
    lf = draw_large(os.path.join(HERE, "large_caldav.bmp"))
    sf = draw_small(os.path.join(HERE, "small_caldav.bmp"))

    lf.convert("L").resize((22 * 14, 22 * 14), Image.NEAREST).save(
        os.path.join(HERE, "preview_large_1bit_x14.png")
    )
    sf.convert("L").resize((15 * 16, 9 * 16), Image.NEAREST).save(
        os.path.join(HERE, "preview_small_1bit_x16.png")
    )
    print("wrote preview PNGs for review")
