#pragma once

#include <EpdFontData.h>

#include <cstddef>
#include <cstdint>
#include <vector>

/// On-device OpenType GPOS/GSUB parser.
/// Extracts kerning pairs (GPOS PairPos Format 1 & 2, legacy kern table)
/// and ligature substitutions (GSUB LigatureSubst type 4) directly from
/// raw TTF/OTF font data.
///
/// All parsing operates on the same `const uint8_t*` that stb_truetype uses --
/// no additional file reads are needed.

namespace opentype {

/// Extract kerning pairs from the font's GPOS table (or legacy kern table).
/// Returns a sorted vector of EpdKernPair with pixel-scaled adjust values.
/// @param fontData  Pointer to the entire TTF/OTF file in memory.
/// @param fontDataSize  Size of the font data in bytes.
/// @param ppem  Pixels-per-em at the target rendering size.
/// @param glyphSet  Set of codepoints that are in the generated glyph set.
///                  Only pairs where both codepoints are in this set are returned.
std::vector<EpdKernPair> extractKerning(const uint8_t* fontData, size_t fontDataSize, float ppem,
                                        const uint32_t* glyphSet, size_t glyphSetSize);

/// Extract ligature substitution pairs from the font's GSUB table.
/// Returns a sorted vector of EpdLigaturePair.
/// Multi-character ligatures are decomposed into chained pairs.
/// @param fontData  Pointer to the entire TTF/OTF file in memory.
/// @param fontDataSize  Size of the font data in bytes.
/// @param glyphSet  Set of codepoints that are in the generated glyph set.
///                  Only pairs where all input and output codepoints are in this set are returned.
std::vector<EpdLigaturePair> extractLigatures(const uint8_t* fontData, size_t fontDataSize,
                                              const uint32_t* glyphSet, size_t glyphSetSize);

}  // namespace opentype
