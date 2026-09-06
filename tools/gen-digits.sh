#!/usr/bin/env bash
# Pre-render the digits 0-9 into sprite-sheet PNGs for the watchface.
# Fonts can't be rasterised big enough on-watch (per-glyph cache), so the digits
# ship as bitmaps. Re-run after changing the font or a target size.
#
#   - faux-bold: only applied when STROKE/BOLD_PX are non-zero — Alfa Slab One
#     is already a black weight, so both stay 0 (stroking it further just
#     closes up the 6/8/9/0 counters)
#   - alpha is quantised to ALPHA_LEVELS even steps (tools/quantize-alpha.py),
#     not a hard binary edge — a pure threshold surviving the SDK's on-watch
#     palette (see below) was jagged on curves/diagonals; a few real steps
#     read smoother while staying a small enough palette that the SDK still
#     encodes it losslessly instead of quantising it further and crushing
#     thin details or small counters like a continuous AA ramp does.
#   - counter-open: Alfa Slab One's counters (0/4/5/6/8/9) are small to begin
#     with, so tools/punch-holes.py grows just the enclosed hole in each glyph
#     (flood-fill from outside the glyph finds what's NOT a hole) — unlike a
#     uniform erode, this leaves the outer silhouette's corners untouched
#     (a global erode was blunting the "4"'s apex)
#   - stroke-thin: a light uniform erode (STROKE_THIN) after the hole is
#     punched, then re-trimmed and rescaled to fill the cell again — cuts the
#     overall "blocky" mass a bit further, on every digit (not just the ones
#     with a counter to open)
#   - shared baseline: `label:` renders every digit into a canvas of the same
#     height (the font's line-height, common to all glyphs) — trimming each
#     digit to its OWN ink bbox throws that away, since flat-top figures
#     (1/4/5/7) don't dip below the baseline the way round ones (0/3/6/8/9)
#     do (~1.4% in this font), and bottom-aligning afterwards then exaggerates
#     that gap. So the first crop uses one shared top/bottom (the union of
#     ink across all 10 digits) instead of each digit's own bbox — only the
#     left/right crop is per-digit.
#

# Needs: ImageMagick (`magick`), Python 3 + Pillow.
set -euo pipefail

cd "$(dirname "$0")/.."
FONT=resources/fonts/AlfaSlabOne-Regular.ttf
OUT_DIR=resources/images
STROKE=0          # base faux-bold, in 400pt-render pixels
BOLD_PX=0         # extra weight added to each digit, in final screen pixels
FILL_SCALE=1.00   # shrink each digit to this fraction of its max_w x max_h cell
COUNTER_HOLE=14   # grows each glyph's enclosed counter, in 400pt-render pixels
STROKE_THIN=8     # light overall erode to cut blockiness, in 400pt-render pixels
ALPHA_LEVELS=8    # even alpha steps (0..255) per glyph edge, instead of binary
MIN_GAP=4         # left-side breathing room reserved in every slot, in final px
mkdir -p "$OUT_DIR"

# build_sheet <name> <max digit width> <max digit height>
build_sheet() {
  local name=$1 max_w=$2 max_h=$3
  local tmp d w h raw_w=0 raw_h=0 pct slot_w=0 slot_h=0
  tmp=$(mktemp -d)
  max_w=$(awk -v m="$max_w" -v s="$FILL_SCALE" 'BEGIN { printf "%d", m * s }')
  max_h=$(awk -v m="$max_h" -v s="$FILL_SCALE" 'BEGIN { printf "%d", m * s }')

  for d in 0 1 2 3 4 5 6 7 8 9; do
    magick -background none -fill white -stroke white -strokewidth "$STROKE" \
      -font "$FONT" -pointsize 400 label:"$d" "$tmp/full$d.png"
  done

  # Shared top/bottom across all 10 digits (see "shared baseline" above);
  # left/right stay per-digit so each glyph is still tightly cropped sideways.
  python3 - "$tmp" <<'PYEOF'
import sys
from PIL import Image
tmp = sys.argv[1]
digits = "0123456789"
alphas = {d: Image.open(f"{tmp}/full{d}.png").split()[-1] for d in digits}
boxes = {d: a.getbbox() for d, a in alphas.items()}
top = min(b[1] for b in boxes.values())
bottom = max(b[3] for b in boxes.values())
for d in digits:
    l, _, r, _ = boxes[d]
    Image.open(f"{tmp}/full{d}.png").crop((l, top, r, bottom)).save(f"{tmp}/raw{d}.png")
PYEOF

  for d in 0 1 2 3 4 5 6 7 8 9; do
    python3 "$(dirname "$0")/punch-holes.py" "$tmp/raw$d.png" "$tmp/hole$d.png" "$COUNTER_HOLE"
    magick "$tmp/hole$d.png" -morphology Erode "Disk:${STROKE_THIN}" "$tmp/r$d.png"
    w=$(magick identify -format '%w' "$tmp/r$d.png")
    h=$(magick identify -format '%h' "$tmp/r$d.png")
    (( w > raw_w )) && raw_w=$w
    (( h > raw_h )) && raw_h=$h
  done

  # Leave room for the dilate so the finished digit still fits max_w x max_h.
  pct=$(awk -v mw="$((max_w - BOLD_PX))" -v mh="$((max_h - BOLD_PX))" \
            -v rw="$raw_w" -v rh="$raw_h" \
        'BEGIN { x = mw/rw; y = mh/rh; printf "%.4f", (x < y ? x : y) * 100 }')

  local radius
  radius=$(awk -v b="$BOLD_PX" 'BEGIN { printf "%.2f", b / 2 }')
  for d in 0 1 2 3 4 5 6 7 8 9; do
    magick "$tmp/r$d.png" -filter Lanczos -resize "${pct}%" \
      -morphology Dilate "Disk:${radius}" \
      "$tmp/rz$d.png"
    python3 "$(dirname "$0")/quantize-alpha.py" "$tmp/rz$d.png" "$tmp/s$d.png" "$ALPHA_LEVELS"
    w=$(magick identify -format '%w' "$tmp/s$d.png")
    h=$(magick identify -format '%h' "$tmp/s$d.png")
    (( w > slot_w )) && slot_w=$w
    (( h > slot_h )) && slot_h=$h
  done

  # Right-aligned (not centred): digit widths vary a lot (e.g. "1" vs "7")
  # but every slot is the same width, so centring gives each digit a
  # different amount of side padding — reads as misaligned once two digits
  # sit side by side. Flush-right keeps the gap consistently on the left.
  #
  # slot_w gets a MIN_GAP pad here (not baked into the per-digit width above)
  # so the widest digit (currently "0", which has zero natural left-bearing)
  # still leaves a gap when it's the right-hand digit of a pair — otherwise
  # it touches whatever's to its left with no seam at all (e.g. "40", "10").
  slot_w=$((slot_w + MIN_GAP))
  for d in 0 1 2 3 4 5 6 7 8 9; do
    magick "$tmp/s$d.png" -background none -gravity SouthEast \
      -extent "${slot_w}x${slot_h}" "$tmp/p$d.png"
  done

  # One row of 10 slots: white ink, hard-edged alpha (see threshold above).
  magick "$tmp"/p{0,1,2,3,4,5,6,7,8,9}.png +append -background none \
    -strip PNG32:"$OUT_DIR/$name.png"
  echo "$OUT_DIR/$name.png  slot ${slot_w}x${slot_h}"
  rm -rf "$tmp"
}

#            name       max_w max_h
build_sheet  digits        88    80   # gabbro (260x260 round)
build_sheet  digits_lg    110   103   # emery  (200x228), overlapped rows
