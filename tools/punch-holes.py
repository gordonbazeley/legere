#!/usr/bin/env python3
# Enlarge a glyph's enclosed counters (the holes in 0/4/5/6/8/9) without
# touching its outer silhouette. A uniform erode blunts sharp corners (the
# "4" apex) along with opening the counters; this only grows background
# that's NOT reachable from outside the glyph.
#
# Usage: punch-holes.py in.png out.png radius_px
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

src, dst, radius = sys.argv[1], sys.argv[2], int(sys.argv[3])

src_img = Image.open(src).convert("RGBA")

# A trimmed glyph's ink touches its own bounding box on all 4 sides (that's
# what "trim" means) — a "2"'s widest point can be its loop at the top and
# its diagonal stroke at the bottom, so ink can touch the top-right AND
# bottom-right of the canvas. Flood-filling straight from a corner then reads
# the untouched exterior between them as a sealed-off "hole". Pad first so
# background can wrap all the way around the glyph before we classify it.
PAD = radius + 10
img = Image.new("RGBA", (src_img.width + 2 * PAD, src_img.height + 2 * PAD), (0, 0, 0, 0))
img.paste(src_img, (PAD, PAD))
alpha = img.split()[-1].point(lambda p: 255 if p > 127 else 0)

# Flood fill is 4-connected, but the hard-thresholded edge of a curve steps
# in single-pixel staircase notches that only touch the true outside
# diagonally — a 4-connected fill misses those and misreads them as enclosed
# holes, then chews them into the ink. Close the background by 1px (bridges
# diagonal-only gaps) just for classifying inside/outside; apply the result
# to the original alpha so real counters keep their exact shape.
bg = alpha.point(lambda p: 0 if p else 255)
bg_closed = bg.filter(ImageFilter.MaxFilter(3)).filter(ImageFilter.MinFilter(3))

mask = bg_closed.copy()
ImageDraw.floodfill(mask, (0, 0), 128, thresh=10)  # mark "outside" background

unreached = mask.point(lambda p: 255 if p == 255 else 0)  # smoothed-background, still unreached
holes = Image.composite(unreached, Image.new("L", alpha.size, 0), bg)  # clip to real background
if holes.getbbox() is None:
    src_img.save(dst)
    sys.exit(0)

# PIL has no round structuring element (MaxFilter's kernel is a square,
# which squares off the grown hole); shell out to ImageMagick's circular
# Disk morphology instead, matching the rest of this pipeline.
with tempfile.TemporaryDirectory() as tmp:
    hin, hout = Path(tmp) / "h.png", Path(tmp) / "h_big.png"
    holes.save(hin)
    subprocess.run(
        ["magick", str(hin), "-morphology", "Dilate", f"Disk:{radius}", str(hout)],
        check=True,
    )
    holes_big = Image.open(hout).convert("L")

new_alpha = Image.composite(Image.new("L", alpha.size, 0), alpha, holes_big)
white = Image.new("RGBA", img.size, (255, 255, 255, 255))
out = Image.composite(white, Image.new("RGBA", img.size, (0, 0, 0, 0)), new_alpha)
out = out.crop((PAD, PAD, PAD + src_img.width, PAD + src_img.height))
out.save(dst)
