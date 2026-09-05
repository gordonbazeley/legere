#!/usr/bin/env python3
# Replace a glyph's alpha with LEVELS evenly-spaced steps (0..255) instead of
# a hard binary edge. A pure threshold gives a jagged staircase on curves and
# diagonals; a few real anti-aliasing steps read smoother while staying a
# small enough palette (<=LEVELS unique colours, ink is solid white) that the
# SDK's on-watch bitmap format still encodes it losslessly instead of
# quantising it further and crushing thin details or small counters.
#
# Usage: quantize-alpha.py in.png out.png levels
import sys
from PIL import Image

src, dst, levels = sys.argv[1], sys.argv[2], int(sys.argv[3])

img = Image.open(src).convert("RGBA")
alpha = img.split()[-1]

# Round (not truncate) to the nearest of `levels` evenly spaced steps, so 0
# and 255 stay exact — a truncating posterize would leave solid ink at
# slightly less than fully opaque.
lut = [round(round(v * (levels - 1) / 255) * 255 / (levels - 1)) for v in range(256)]
alpha = alpha.point(lut)

white = Image.new("RGBA", img.size, (255, 255, 255, 255))
out = Image.new("RGBA", img.size, (255, 255, 255, 0))
out.paste(white, (0, 0), alpha)
out.save(dst)
