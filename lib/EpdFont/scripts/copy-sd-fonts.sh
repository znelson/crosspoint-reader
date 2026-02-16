#!/bin/bash
# Copy reader font TTF/OTF files to the SD card font directory structure.
# Usage: ./copy-sd-fonts.sh <sd-card-mount-point>
#
# Creates:
#   <mount>/fonts/NotoSans/NotoSans-{Regular,Bold,Italic,BoldItalic}.ttf
#   <mount>/fonts/OpenDyslexic/OpenDyslexic-{Regular,Bold,Italic,BoldItalic}.otf

set -e

if [ -z "$1" ]; then
  echo "Usage: $0 <sd-card-mount-point>"
  exit 1
fi

SD="$1"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SOURCE_DIR="$SCRIPT_DIR/../builtinFonts/source"

mkdir -p "$SD/fonts/NotoSans"
mkdir -p "$SD/fonts/OpenDyslexic"

cp "$SOURCE_DIR/NotoSans/NotoSans-Regular.ttf"    "$SD/fonts/NotoSans/"
cp "$SOURCE_DIR/NotoSans/NotoSans-Bold.ttf"        "$SD/fonts/NotoSans/"
cp "$SOURCE_DIR/NotoSans/NotoSans-Italic.ttf"      "$SD/fonts/NotoSans/"
cp "$SOURCE_DIR/NotoSans/NotoSans-BoldItalic.ttf"  "$SD/fonts/NotoSans/"

cp "$SOURCE_DIR/OpenDyslexic/OpenDyslexic-Regular.otf"    "$SD/fonts/OpenDyslexic/"
cp "$SOURCE_DIR/OpenDyslexic/OpenDyslexic-Bold.otf"        "$SD/fonts/OpenDyslexic/"
cp "$SOURCE_DIR/OpenDyslexic/OpenDyslexic-Italic.otf"      "$SD/fonts/OpenDyslexic/"
cp "$SOURCE_DIR/OpenDyslexic/OpenDyslexic-BoldItalic.otf"  "$SD/fonts/OpenDyslexic/"

echo "SD card fonts copied to $SD/fonts/"
