#!/usr/bin/env bash
# Pre-render the digits 0-9 into sprite-sheet PNGs for the watchface.
# Fonts can't be rasterised big enough on-watch (per-glyph cache), so the digits
# ship as bitmaps. Re-run after changing the font or a target size.
#
#   - faux-bold: Michroma has one weight, so we stroke the outline
#   - anti-aliased: kept as a small grey/alpha ramp, not thresholded to 1-bit
#
# Needs: ImageMagick (`magick`).
set -euo pipefail

cd "$(dirname "$0")/.."
FONT=resources/fonts/Michroma-Regular.ttf
OUT_DIR=resources/images
STROKE=8          # faux-bold, in 400pt-render pixels
mkdir -p "$OUT_DIR"

# build_sheet <name> <max digit width> <max digit height>
build_sheet() {
  local name=$1 max_w=$2 max_h=$3
  local tmp d w h raw_w=0 raw_h=0 pct slot_w=0 slot_h=0
  tmp=$(mktemp -d)

  for d in 0 1 2 3 4 5 6 7 8 9; do
    magick -background none -fill white -stroke white -strokewidth "$STROKE" \
      -font "$FONT" -pointsize 400 label:"$d" -trim +repage "$tmp/r$d.png"
    w=$(magick identify -format '%w' "$tmp/r$d.png")
    h=$(magick identify -format '%h' "$tmp/r$d.png")
    (( w > raw_w )) && raw_w=$w
    (( h > raw_h )) && raw_h=$h
  done

  pct=$(awk -v mw="$max_w" -v mh="$max_h" -v rw="$raw_w" -v rh="$raw_h" \
        'BEGIN { x = mw/rw; y = mh/rh; printf "%.4f", (x < y ? x : y) * 100 }')

  for d in 0 1 2 3 4 5 6 7 8 9; do
    magick "$tmp/r$d.png" -filter Lanczos -resize "${pct}%" "$tmp/s$d.png"
    w=$(magick identify -format '%w' "$tmp/s$d.png")
    h=$(magick identify -format '%h' "$tmp/s$d.png")
    (( w > slot_w )) && slot_w=$w
    (( h > slot_h )) && slot_h=$h
  done

  for d in 0 1 2 3 4 5 6 7 8 9; do
    magick "$tmp/s$d.png" -background none -gravity SouthEast \
      -extent "${slot_w}x${slot_h}" "$tmp/p$d.png"
  done

  # One row of 10 slots: white ink, anti-aliased edge kept as an alpha ramp.
  # The SDK's bitmapgen quantises this to a small palette bitmap at build time.
  magick "$tmp"/p{0,1,2,3,4,5,6,7,8,9}.png +append -background none \
    -strip PNG32:"$OUT_DIR/$name.png"
  echo "$OUT_DIR/$name.png  slot ${slot_w}x${slot_h}"
  rm -rf "$tmp"
}

#            name       max_w max_h
build_sheet  digits        66    62   # gabbro (180x180 round)
build_sheet  digits_lg     92    98   # emery  (200x228)
