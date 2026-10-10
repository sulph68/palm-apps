#!/usr/bin/env python3
# mkicon_sec35.py - Sec35 launcher icon.
#
# The large icon is the user's EXACT artwork (icons/icon35_large_src.bmp) with a
# "35" badge stamped in the bottom-right corner using the IDENTICAL routine as
# datebook35 (apps/mkicons.py overlay35, font size 10: convert to grayscale, draw
# a white badge box over the corner, then the black "35" on top). No redrawing.
#
# Outputs (self-contained under sec35/icons/):
#   icon35_large.bmp  - large tAIB 1000: your icon + "35" badge
#   icon35_small.bmp  - small tAIB 1001: a simple key
import os
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ICONS = os.path.join(HERE, "icons")
SRC = os.path.join(ICONS, "icon35_large_src.bmp")   # the user's pristine icon


def _font(sz):
    for p in ["/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
              "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf"]:
        if os.path.exists(p):
            return ImageFont.truetype(p, sz)
    return ImageFont.load_default()


def overlay35(rompng, out, fsz):
    # verbatim from apps/mkicons.py (the datebook35 badge routine)
    im = Image.open(rompng).convert("L"); w, h = im.size; d = ImageDraw.Draw(im)
    f = _font(fsz)
    l, t, r, b = d.textbbox((0, 0), "35", font=f)
    tw, th = r - l, b - t
    pad = 1
    bx1, by1 = w - tw - 2 * pad, h - th - 2 * pad
    bx0, by0 = bx1, by1
    d.rectangle([bx0 - 1, by0 - 1, w - 1, h - 1], fill=255)
    d.text((bx0 - l, by0 - t), "35", font=f, fill=0)
    im.save(out, "BMP"); return im


def draw_key_small(out, w=15, h=9):
    s = 12
    im = Image.new("L", (w * s, h * s), 255)
    d = ImageDraw.Draw(im)
    cy = 4 * s + s // 2
    r = int(2.9 * s)
    cx = int(3.3 * s)
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=0)
    d.ellipse([cx - int(1.3 * s), cy - int(1.3 * s), cx + int(1.3 * s), cy + int(1.3 * s)], fill=255)
    sw = int(1.5 * s)
    tip = int(13.2 * s)
    d.line([(cx + r - s, cy), (tip, cy)], fill=0, width=sw)
    d.line([(tip, cy - sw // 2), (tip, cy + int(2.6 * s))], fill=0, width=sw)
    d.line([(tip - int(2.6 * s), cy), (tip - int(2.6 * s), cy + int(2.4 * s))], fill=0, width=sw)
    im.resize((w, h), Image.LANCZOS).point(lambda p: 0 if p < 150 else 255).convert("1").save(out, "BMP")


overlay35(SRC, os.path.join(ICONS, "icon35_large.bmp"), 10)
draw_key_small(os.path.join(ICONS, "icon35_small.bmp"))
print("wrote icon35_large.bmp (your icon + 35 badge) + icon35_small.bmp")
