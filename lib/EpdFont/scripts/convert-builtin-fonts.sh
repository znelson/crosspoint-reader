#!/bin/bash

set -e

cd "$(dirname "$0")"

# UI fonts (1-bit, compiled into binary)
UI_FONT_SIZES=(10 12)
UI_FONT_STYLES=("Regular" "Bold")

for size in ${UI_FONT_SIZES[@]}; do
  for style in ${UI_FONT_STYLES[@]}; do
    font_name="ubuntu_${size}_$(echo $style | tr '[:upper:]' '[:lower:]')"
    font_path="../builtinFonts/source/Ubuntu/Ubuntu-${style}.ttf"
    output_path="../builtinFonts/${font_name}.h"
    python fontconvert.py $font_name $size $font_path --kern-scope english > $output_path
    echo "Generated $output_path"
  done
done

# Small font for status bar etc. (1-bit, compiled into binary)
python fontconvert.py notosans_8_regular 8 ../builtinFonts/source/NotoSans/NotoSans-Regular.ttf --kern-scope english > ../builtinFonts/notosans_8_regular.h

# Reader fonts (Bookerly, NotoSans 12-18, OpenDyslexic) are no longer
# pre-rasterized at build time. They are rasterized on-device from TTF files
# using stb_truetype and cached in the fontcache flash partition.
