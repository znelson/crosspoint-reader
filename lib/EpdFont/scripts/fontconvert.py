#!python3
import freetype
import zlib
import sys
import re
import math
import argparse
from collections import namedtuple
from fontTools.ttLib import TTFont

# Originally from https://github.com/vroland/epdiy

parser = argparse.ArgumentParser(description="Generate a header file from a font to be used with epdiy.")
parser.add_argument("name", action="store", help="name of the font.")
parser.add_argument("size", type=int, help="font size to use.")
parser.add_argument("fontstack", action="store", nargs='+', help="list of font files, ordered by descending priority.")
parser.add_argument("--2bit", dest="is2Bit", action="store_true", help="generate 2-bit greyscale bitmap instead of 1-bit black and white.")
parser.add_argument("--additional-intervals", dest="additional_intervals", action="append", help="Additional code point intervals to export as min,max. This argument can be repeated.")
parser.add_argument("--kern-scope", dest="kern_scope", choices=["all", "english"], default="all", help="Restrict kerning extraction to a character subset. 'english' limits to ASCII + Latin-1 accented + typographic punctuation (~200 chars). Default: all.")
args = parser.parse_args()

GlyphProps = namedtuple("GlyphProps", ["width", "height", "advance_x", "left", "top", "data_length", "data_offset", "code_point"])

font_stack = [freetype.Face(f) for f in args.fontstack]
is2Bit = args.is2Bit
size = args.size
font_name = args.name

# inclusive unicode code point intervals
# must not overlap and be in ascending order
intervals = [
    ### Basic Latin ###
    # ASCII letters, digits, punctuation, control characters
    (0x0000, 0x007F),
    ### Latin-1 Supplement ###
    # Accented characters for Western European languages
    (0x0080, 0x00FF),
    ### Latin Extended-A ###
    # Eastern European and Baltic languages
    (0x0100, 0x017F),
    ### General Punctuation (core subset) ###
    # Smart quotes, en dash, em dash, ellipsis, NO-BREAK SPACE
    (0x2000, 0x206F),
    ### Basic Symbols From "Latin-1 + Misc" ###
    # dashes, quotes, prime marks
    (0x2010, 0x203A),
    # misc punctuation
    (0x2040, 0x205F),
    # common currency symbols
    (0x20A0, 0x20CF),
    ### Combining Diacritical Marks (minimal subset) ###
    # Needed for proper rendering of many extended Latin languages
    (0x0300, 0x036F),
    ### Greek & Coptic ###
    # Used in science, maths, philosophy, some academic texts
    # (0x0370, 0x03FF),
    ### Cyrillic ###
    # Russian, Ukrainian, Bulgarian, etc.
    (0x0400, 0x04FF),
    ### Math Symbols (common subset) ###
    # Superscripts and Subscripts
    (0x2070, 0x209F),
    # General math operators
    (0x2200, 0x22FF),
    # Arrows
    (0x2190, 0x21FF),
    ### CJK ###
    # Core Unified Ideographs
    # (0x4E00, 0x9FFF),
    # # Extension A
    # (0x3400, 0x4DBF),
    # # Extension B
    # (0x20000, 0x2A6DF),
    # # Extension C–F
    # (0x2A700, 0x2EBEF),
    # # Extension G
    # (0x30000, 0x3134F),
    # # Hiragana
    # (0x3040, 0x309F),
    # # Katakana
    # (0x30A0, 0x30FF),
    # # Katakana Phonetic Extensions
    # (0x31F0, 0x31FF),
    # # Halfwidth Katakana
    # (0xFF60, 0xFF9F),
    # # Hangul Syllables
    # (0xAC00, 0xD7AF),
    # # Hangul Jamo
    # (0x1100, 0x11FF),
    # # Hangul Compatibility Jamo
    # (0x3130, 0x318F),
    # # Hangul Jamo Extended-A
    # (0xA960, 0xA97F),
    # # Hangul Jamo Extended-B
    # (0xD7B0, 0xD7FF),
    # # CJK Radicals Supplement
    # (0x2E80, 0x2EFF),
    # # Kangxi Radicals
    # (0x2F00, 0x2FDF),
    # # CJK Symbols and Punctuation
    # (0x3000, 0x303F),
    # # CJK Compatibility Forms
    # (0xFE30, 0xFE4F),
    # # CJK Compatibility Ideographs
    # (0xF900, 0xFAFF),
    ### Specials
    # Replacement Character
    (0xFFFD, 0xFFFD),
]

add_ints = []
if args.additional_intervals:
    add_ints = [tuple([int(n, base=0) for n in i.split(",")]) for i in args.additional_intervals]

def norm_floor(val):
    return int(math.floor(val / (1 << 6)))

def norm_ceil(val):
    return int(math.ceil(val / (1 << 6)))

def chunks(l, n):
    for i in range(0, len(l), n):
        yield l[i:i + n]

def load_glyph(code_point):
    face_index = 0
    while face_index < len(font_stack):
        face = font_stack[face_index]
        glyph_index = face.get_char_index(code_point)
        if glyph_index > 0:
            face.load_glyph(glyph_index, freetype.FT_LOAD_RENDER)
            return face
        face_index += 1
    print(f"code point {code_point} ({hex(code_point)}) not found in font stack!", file=sys.stderr)
    return None

unmerged_intervals = sorted(intervals + add_ints)
intervals = []
unvalidated_intervals = []
for i_start, i_end in unmerged_intervals:
    if len(unvalidated_intervals) > 0 and i_start + 1 <= unvalidated_intervals[-1][1]:
        unvalidated_intervals[-1] = (unvalidated_intervals[-1][0], max(unvalidated_intervals[-1][1], i_end))
        continue
    unvalidated_intervals.append((i_start, i_end))

for i_start, i_end in unvalidated_intervals:
    start = i_start
    for code_point in range(i_start, i_end + 1):
        face = load_glyph(code_point)
        if face is None:
            if start < code_point:
                intervals.append((start, code_point - 1))
            start = code_point + 1
    if start != i_end + 1:
        intervals.append((start, i_end))

for face in font_stack:
    face.set_char_size(size << 6, size << 6, 150, 150)

total_size = 0
all_glyphs = []

for i_start, i_end in intervals:
    for code_point in range(i_start, i_end + 1):
        face = load_glyph(code_point)
        bitmap = face.glyph.bitmap

        # Build out 4-bit greyscale bitmap
        pixels4g = []
        px = 0
        for i, v in enumerate(bitmap.buffer):
            y = i / bitmap.width
            x = i % bitmap.width
            if x % 2 == 0:
                px = (v >> 4)
            else:
                px = px | (v & 0xF0)
                pixels4g.append(px);
                px = 0
            # eol
            if x == bitmap.width - 1 and bitmap.width % 2 > 0:
                pixels4g.append(px)
                px = 0

        if is2Bit:
            # 0-3 white, 4-7 light grey, 8-11 dark grey, 12-15 black
            # Downsample to 2-bit bitmap
            pixels2b = []
            px = 0
            pitch = (bitmap.width // 2) + (bitmap.width % 2)
            for y in range(bitmap.rows):
                for x in range(bitmap.width):
                    px = px << 2
                    bm = pixels4g[y * pitch + (x // 2)]
                    bm = (bm >> ((x % 2) * 4)) & 0xF

                    if bm >= 12:
                        px += 3
                    elif bm >= 8:
                        px += 2
                    elif bm >= 4:
                        px += 1

                    if (y * bitmap.width + x) % 4 == 3:
                        pixels2b.append(px)
                        px = 0
            if (bitmap.width * bitmap.rows) % 4 != 0:
                px = px << (4 - (bitmap.width * bitmap.rows) % 4) * 2
                pixels2b.append(px)

            # for y in range(bitmap.rows):
            #     line = ''
            #     for x in range(bitmap.width):
            #         pixelPosition = y * bitmap.width + x
            #         byte = pixels2b[pixelPosition // 4]
            #         bit_index = (3 - (pixelPosition % 4)) * 2
            #         line += '#' if ((byte >> bit_index) & 3) > 0 else '.'
            #     print(line)
            # print('')
        else:
            # Downsample to 1-bit bitmap - treat any 2+ as black
            pixelsbw = []
            px = 0
            pitch = (bitmap.width // 2) + (bitmap.width % 2)
            for y in range(bitmap.rows):
                for x in range(bitmap.width):
                    px = px << 1
                    bm = pixels4g[y * pitch + (x // 2)]
                    px += 1 if ((x & 1) == 0 and bm & 0xE > 0) or ((x & 1) == 1 and bm & 0xE0 > 0) else 0

                    if (y * bitmap.width + x) % 8 == 7:
                        pixelsbw.append(px)
                        px = 0
            if (bitmap.width * bitmap.rows) % 8 != 0:
                px = px << (8 - (bitmap.width * bitmap.rows) % 8)
                pixelsbw.append(px)

            # for y in range(bitmap.rows):
            #     line = ''
            #     for x in range(bitmap.width):
            #         pixelPosition = y * bitmap.width + x
            #         byte = pixelsbw[pixelPosition // 8]
            #         bit_index = 7 - (pixelPosition % 8)
            #         line += '#' if (byte >> bit_index) & 1 else '.'
            #     print(line)
            # print('')

        pixels = pixels2b if is2Bit else pixelsbw

        # Build output data
        packed = bytes(pixels)
        glyph = GlyphProps(
            width = bitmap.width,
            height = bitmap.rows,
            advance_x = norm_floor(face.glyph.advance.x),
            left = face.glyph.bitmap_left,
            top = face.glyph.bitmap_top,
            data_length = len(packed),
            data_offset = total_size,
            code_point = code_point,
        )
        total_size += len(packed)
        all_glyphs.append((glyph, packed))

# pipe seems to be a good heuristic for the "real" descender
face = load_glyph(ord('|'))

glyph_data = []
glyph_props = []
for index, glyph in enumerate(all_glyphs):
    props, packed = glyph
    glyph_data.extend([b for b in packed])
    glyph_props.append(props)

# --- Kerning pair extraction ---
# Modern fonts store kerning in the OpenType GPOS table, which FreeType's
# get_kerning() does not read. We use fonttools to parse both the legacy
# kern table and the GPOS 'kern' feature (PairPos lookups, including
# Extension wrappers).

COMBINING_MARKS_START = 0x0300
COMBINING_MARKS_END = 0x036F
all_codepoints = [g.code_point for g in glyph_props]
kernable_codepoints = set(cp for cp in all_codepoints
                          if not (COMBINING_MARKS_START <= cp <= COMBINING_MARKS_END))

# "english" kerning scope: ASCII printable + Latin-1 Supplement + typographic punctuation
KERN_ENGLISH_CODEPOINTS = (
    frozenset(range(0x0020, 0x007F)) |  # ASCII printable
    frozenset(range(0x00A0, 0x0100)) |  # Latin-1 Supplement (accented chars, symbols)
    frozenset({0x2013, 0x2014,          # en dash, em dash
               0x2018, 0x2019,          # left/right single quote (apostrophe)
               0x201A,                  # single low-9 quotation mark
               0x201C, 0x201D,          # left/right double quote
               0x201E,                  # double low-9 quotation mark
               0x2026})                 # horizontal ellipsis
)

if args.kern_scope == 'english':
    kernable_codepoints &= KERN_ENGLISH_CODEPOINTS
    print(f"kerning: scope limited to 'english' ({len(kernable_codepoints)} kernable codepoints)", file=sys.stderr)

# Map each kernable codepoint to the font-stack index that serves it
# (same priority logic as load_glyph).
cp_to_face_idx = {}
for cp in kernable_codepoints:
    for face_idx, f in enumerate(font_stack):
        if f.get_char_index(cp) > 0:
            cp_to_face_idx[cp] = face_idx
            break

# Group codepoints by face index
face_idx_cps = {}
for cp, fi in cp_to_face_idx.items():
    face_idx_cps.setdefault(fi, set()).add(cp)

def _extract_pairpos_subtable(subtable, glyph_to_cp, raw_kern):
    """Extract kerning from a PairPos subtable (Format 1 or 2)."""
    if subtable.Format == 1:
        # Individual pairs
        for i, coverage_glyph in enumerate(subtable.Coverage.glyphs):
            if coverage_glyph not in glyph_to_cp:
                continue
            pair_set = subtable.PairSet[i]
            for pvr in pair_set.PairValueRecord:
                if pvr.SecondGlyph not in glyph_to_cp:
                    continue
                xa = 0
                if hasattr(pvr, 'Value1') and pvr.Value1:
                    xa = getattr(pvr.Value1, 'XAdvance', 0) or 0
                if xa != 0:
                    key = (coverage_glyph, pvr.SecondGlyph)
                    raw_kern[key] = raw_kern.get(key, 0) + xa
    elif subtable.Format == 2:
        # Class-based pairs
        class_def1 = subtable.ClassDef1.classDefs if subtable.ClassDef1 else {}
        class_def2 = subtable.ClassDef2.classDefs if subtable.ClassDef2 else {}
        coverage_set = set(subtable.Coverage.glyphs)
        for left_glyph in glyph_to_cp:
            if left_glyph not in coverage_set:
                continue
            c1 = class_def1.get(left_glyph, 0)
            if c1 >= len(subtable.Class1Record):
                continue
            class1_rec = subtable.Class1Record[c1]
            for right_glyph in glyph_to_cp:
                c2 = class_def2.get(right_glyph, 0)
                if c2 >= len(class1_rec.Class2Record):
                    continue
                c2_rec = class1_rec.Class2Record[c2]
                xa = 0
                if hasattr(c2_rec, 'Value1') and c2_rec.Value1:
                    xa = getattr(c2_rec.Value1, 'XAdvance', 0) or 0
                if xa != 0:
                    key = (left_glyph, right_glyph)
                    raw_kern[key] = raw_kern.get(key, 0) + xa

def extract_kerning_fonttools(font_path, codepoints, ppem):
    """Extract kerning pairs from a font file using fonttools.

    Returns list of (packed_pair, pixel_adjust) for the given codepoints.
    Values are scaled from font design units to integer pixels at ppem.
    """
    font = TTFont(font_path)
    units_per_em = font['head'].unitsPerEm
    cmap = font.getBestCmap() or {}

    # Build glyph_name -> codepoint map (only for requested codepoints)
    glyph_to_cp = {}
    for cp in codepoints:
        gname = cmap.get(cp)
        if gname:
            glyph_to_cp[gname] = cp

    # Collect raw kerning values in font design units
    raw_kern = {}  # (left_glyph_name, right_glyph_name) -> design_units

    # 1. Legacy kern table
    if 'kern' in font:
        for subtable in font['kern'].kernTables:
            if hasattr(subtable, 'kernTable'):
                for (lg, rg), val in subtable.kernTable.items():
                    if lg in glyph_to_cp and rg in glyph_to_cp:
                        raw_kern[(lg, rg)] = raw_kern.get((lg, rg), 0) + val

    # 2. GPOS 'kern' feature
    if 'GPOS' in font:
        gpos = font['GPOS'].table
        kern_lookup_indices = set()
        if gpos.FeatureList:
            for fr in gpos.FeatureList.FeatureRecord:
                if fr.FeatureTag == 'kern':
                    kern_lookup_indices.update(fr.Feature.LookupListIndex)
        for li in kern_lookup_indices:
            lookup = gpos.LookupList.Lookup[li]
            for st in lookup.SubTable:
                actual = st
                # Unwrap Extension (lookup type 9) wrappers
                if lookup.LookupType == 9 and hasattr(st, 'ExtSubTable'):
                    actual = st.ExtSubTable
                if hasattr(actual, 'Format'):
                    _extract_pairpos_subtable(actual, glyph_to_cp, raw_kern)

    font.close()

    # Scale design-unit values to pixels and pack
    scale = ppem / units_per_em
    pairs = []
    for (lg, rg), du in raw_kern.items():
        lcp = glyph_to_cp[lg]
        rcp = glyph_to_cp[rg]
        adjust = int(math.floor(du * scale))
        if adjust != 0:
            adjust = max(-128, min(127, adjust))
            pairs.append(((lcp << 16) | rcp, adjust))
    return pairs

# The ppem used by the existing glyph rasterization:
#   face.set_char_size(size << 6, size << 6, 150, 150)
# means size_pt at 150 DPI -> ppem = size * 150 / 72
ppem = size * 150.0 / 72.0

kern_pairs = []
for face_idx, cps in face_idx_cps.items():
    font_path = args.fontstack[face_idx]
    kern_pairs.extend(extract_kerning_fonttools(font_path, cps, ppem))

kern_pairs.sort(key=lambda p: p[0])
print(f"kerning: {len(kern_pairs)} pairs extracted", file=sys.stderr)

print(f"""/**
 * generated by fontconvert.py
 * name: {font_name}
 * size: {size}
 * mode: {'2-bit' if is2Bit else '1-bit'}
 * Command used: {' '.join(sys.argv)}
 */
#pragma once
#include "EpdFontData.h"
""")

print(f"static const uint8_t {font_name}Bitmaps[{len(glyph_data)}] = {{")
for c in chunks(glyph_data, 16):
    print ("    " + " ".join(f"0x{b:02X}," for b in c))
print ("};\n");

print(f"static const EpdGlyph {font_name}Glyphs[] = {{")
for i, g in enumerate(glyph_props):
    print ("    { " + ", ".join([f"{a}" for a in list(g[:-1])]),"},", f"// {chr(g.code_point) if g.code_point != 92 else '<backslash>'}")
print ("};\n");

print(f"static const EpdUnicodeInterval {font_name}Intervals[] = {{")
offset = 0
for i_start, i_end in intervals:
    print (f"    {{ 0x{i_start:X}, 0x{i_end:X}, 0x{offset:X} }},")
    offset += i_end - i_start + 1
print ("};\n");

if kern_pairs:
    print(f"static const EpdKernPair {font_name}KernPairs[] = {{")
    for packed_pair, adjust in kern_pairs:
        left_cp = packed_pair >> 16
        right_cp = packed_pair & 0xFFFF
        comment_l = chr(left_cp) if 0x20 < left_cp < 0x7F else f'U+{left_cp:04X}'
        comment_r = chr(right_cp) if 0x20 < right_cp < 0x7F else f'U+{right_cp:04X}'
        print(f"    {{ 0x{packed_pair:08X}, {adjust} }}, // {comment_l} {comment_r}")
    print("};\n")

print(f"static const EpdFontData {font_name} = {{")
print(f"    {font_name}Bitmaps,")
print(f"    {font_name}Glyphs,")
print(f"    {font_name}Intervals,")
print(f"    {len(intervals)},")
print(f"    {norm_ceil(face.size.height)},")
print(f"    {norm_ceil(face.size.ascender)},")
print(f"    {norm_floor(face.size.descender)},")
print(f"    {'true' if is2Bit else 'false'},")
if kern_pairs:
    print(f"    {font_name}KernPairs,")
    print(f"    {len(kern_pairs)},")
else:
    print(f"    nullptr,")
    print(f"    0,")
print("};")
