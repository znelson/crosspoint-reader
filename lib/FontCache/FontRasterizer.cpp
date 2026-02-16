#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include "FontRasterizer.h"
#include "OpenTypeKernLig.h"

#include <algorithm>
#include <cmath>
#include <cstring>

// Unicode intervals matching fontconvert.py -- must be in ascending, non-overlapping order.
static const struct { uint32_t first; uint32_t last; } kIntervals[] = {
    {0x0000, 0x007F},   // Basic Latin (ASCII)
    {0x0080, 0x00FF},   // Latin-1 Supplement
    {0x0100, 0x017F},   // Latin Extended-A
    {0x0300, 0x036F},   // Combining Diacritical Marks
    {0x0400, 0x04FF},   // Cyrillic
    {0x2000, 0x206F},   // General Punctuation
    {0x2070, 0x209F},   // Superscripts and Subscripts
    {0x20A0, 0x20CF},   // Currency Symbols
    {0x2190, 0x21FF},   // Arrows
    {0x2200, 0x22FF},   // Mathematical Operators
    {0xFB00, 0xFB06},   // Alphabetic Presentation Forms (ligatures)
    {0xFFFD, 0xFFFD},   // Replacement Character
};
static constexpr int kIntervalCount = sizeof(kIntervals) / sizeof(kIntervals[0]);

// Downsample an 8-bit alpha value to 2-bit (0-3).
// Thresholds match fontconvert.py's FreeType 4-bit downsampling:
//   FreeType 4-bit 0-3 (8-bit 0-63)   → 0 (white)
//   FreeType 4-bit 4-7 (8-bit 64-127)  → 1 (light gray)
//   FreeType 4-bit 8-11 (8-bit 128-191) → 2 (dark gray)
//   FreeType 4-bit 12-15 (8-bit 192-255) → 3 (black)
static inline uint8_t alpha8to2bit(uint8_t a) {
    if (a < 64) return 0;
    if (a < 128) return 1;
    if (a < 192) return 2;
    return 3;
}

// Pack a row of 2-bit pixels into bytes (MSB first, 4 pixels per byte).
static void pack2bitRow(const uint8_t* alpha, int width, std::vector<uint8_t>& out) {
    int i = 0;
    while (i < width) {
        uint8_t byte = 0;
        for (int bit = 0; bit < 4 && i < width; bit++, i++) {
            byte |= alpha8to2bit(alpha[i]) << (6 - bit * 2);
        }
        out.push_back(byte);
    }
}

std::vector<uint8_t> FontRasterizer::rasterize(const uint8_t* fontData, size_t fontDataSize,
                                                int pixelSize, ProgressCallback progress) {
    stbtt_fontinfo info;
    if (!stbtt_InitFont(&info, fontData, stbtt_GetFontOffsetForIndex(fontData, 0))) {
        return {};
    }

    // Scale factor: fontconvert.py uses freetype at 150 DPI with char_size = size << 6
    // This gives ppem = size * 150 / 72 = size * 2.083...
    // stb_truetype's ScaleForPixelHeight is equivalent.
    float ppem = pixelSize * 150.0f / 72.0f;
    float scale = stbtt_ScaleForPixelHeight(&info, ppem);

    // Get font metrics
    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(&info, &ascent, &descent, &lineGap);
    int advanceY = (int)std::ceil((ascent - descent + lineGap) * scale);
    int scaledAscender = (int)std::ceil(ascent * scale);
    int scaledDescender = (int)std::floor(descent * scale);

    // Count total codepoints
    int totalCodepoints = 0;
    for (int i = 0; i < kIntervalCount; i++) {
        totalCodepoints += kIntervals[i].last - kIntervals[i].first + 1;
    }

    // Build a flat list of all codepoints for kerning/ligature extraction
    std::vector<uint32_t> allCodepoints;
    allCodepoints.reserve(totalCodepoints);
    for (int i = 0; i < kIntervalCount; i++) {
        for (uint32_t cp = kIntervals[i].first; cp <= kIntervals[i].last; cp++) {
            allCodepoints.push_back(cp);
        }
    }

    // Rasterize all glyphs
    std::vector<uint8_t> bitmapData;
    std::vector<EpdGlyph> glyphs;
    glyphs.reserve(totalCodepoints);

    int codepointsDone = 0;
    for (int intIdx = 0; intIdx < kIntervalCount; intIdx++) {
        for (uint32_t cp = kIntervals[intIdx].first; cp <= kIntervals[intIdx].last; cp++) {
            int glyphIndex = stbtt_FindGlyphIndex(&info, cp);

            int advanceWidth, lsb;
            stbtt_GetGlyphHMetrics(&info, glyphIndex, &advanceWidth, &lsb);

            int x0, y0, x1, y1;
            stbtt_GetGlyphBitmapBox(&info, glyphIndex, scale, scale, &x0, &y0, &x1, &y1);

            int w = x1 - x0;
            int h = y1 - y0;

            EpdGlyph g = {};
            g.width = (uint8_t)std::min(w, 255);
            g.height = (uint8_t)std::min(h, 255);
            g.advanceX = (uint8_t)std::min((int)std::floor(advanceWidth * scale), 255);
            g.left = (int16_t)x0;
            g.top = (int16_t)(-y0);  // epdiy convention: top = baseline to top of glyph (positive up)
            g.dataOffset = (uint32_t)bitmapData.size();

            if (w > 0 && h > 0 && glyphIndex != 0) {
                // Render 8-bit alpha bitmap
                std::vector<uint8_t> alpha(w * h);
                stbtt_MakeGlyphBitmap(&info, alpha.data(), w, h, w, scale, scale, glyphIndex);

                // Pack to 2-bit
                for (int row = 0; row < h; row++) {
                    pack2bitRow(alpha.data() + row * w, w, bitmapData);
                }
            }

            g.dataLength = (uint16_t)((uint32_t)bitmapData.size() - g.dataOffset);
            glyphs.push_back(g);

            codepointsDone++;
            if (progress && (codepointsDone % 64 == 0)) {
                // Rasterization is ~80% of the work; kern/lig parsing is the remaining ~20%.
                progress(codepointsDone * 80 / totalCodepoints);
            }
        }
    }

    if (progress) progress(80);

    // Build interval array
    std::vector<EpdUnicodeInterval> intervals;
    intervals.reserve(kIntervalCount);
    uint32_t offset = 0;
    for (int i = 0; i < kIntervalCount; i++) {
        intervals.push_back({kIntervals[i].first, kIntervals[i].last, offset});
        offset += kIntervals[i].last - kIntervals[i].first + 1;
    }

    // Extract kerning from GPOS (or legacy kern table)
    auto kernPairs = opentype::extractKerning(fontData, fontDataSize, ppem,
                                              allCodepoints.data(), allCodepoints.size());

    // If GPOS yielded nothing, try stb_truetype's legacy kern table reader
    if (kernPairs.empty()) {
        for (size_t i = 0; i < allCodepoints.size(); i++) {
            int g1 = stbtt_FindGlyphIndex(&info, allCodepoints[i]);
            if (g1 == 0) continue;
            for (size_t j = 0; j < allCodepoints.size(); j++) {
                int g2 = stbtt_FindGlyphIndex(&info, allCodepoints[j]);
                if (g2 == 0) continue;
                int kern = stbtt_GetGlyphKernAdvance(&info, g1, g2);
                if (kern == 0) continue;
                int adj = (int)std::floor(kern * scale);
                if (adj == 0) continue;
                if (adj < -128) adj = -128;
                if (adj > 127) adj = 127;
                uint32_t packed = (allCodepoints[i] << 16) | (allCodepoints[j] & 0xFFFF);
                kernPairs.push_back({packed, (int8_t)adj});
            }
        }
        std::sort(kernPairs.begin(), kernPairs.end(),
                  [](const EpdKernPair& a, const EpdKernPair& b) { return a.pair < b.pair; });
    }

    if (progress) progress(90);

    // Extract ligatures from GSUB
    auto ligPairs = opentype::extractLigatures(fontData, fontDataSize,
                                               allCodepoints.data(), allCodepoints.size());

    if (progress) progress(95);

    // --- Serialize the blob ---
    BlobHeader header = {};
    header.bitmapSize = (uint32_t)bitmapData.size();
    header.glyphCount = (uint32_t)glyphs.size();
    header.intervalCount = (uint32_t)intervals.size();
    header.kernPairCount = (uint32_t)kernPairs.size();
    header.ligaturePairCount = (uint32_t)ligPairs.size();
    header.advanceY = (uint8_t)advanceY;
    header.ascender = scaledAscender;
    header.descender = scaledDescender;
    header.is2Bit = 1;

    size_t totalSize = sizeof(BlobHeader)
        + bitmapData.size()
        + glyphs.size() * sizeof(EpdGlyph)
        + intervals.size() * sizeof(EpdUnicodeInterval)
        + kernPairs.size() * sizeof(EpdKernPair)
        + ligPairs.size() * sizeof(EpdLigaturePair);

    std::vector<uint8_t> blob(totalSize);
    uint8_t* ptr = blob.data();

    memcpy(ptr, &header, sizeof(BlobHeader));
    ptr += sizeof(BlobHeader);

    memcpy(ptr, bitmapData.data(), bitmapData.size());
    ptr += bitmapData.size();

    memcpy(ptr, glyphs.data(), glyphs.size() * sizeof(EpdGlyph));
    ptr += glyphs.size() * sizeof(EpdGlyph);

    memcpy(ptr, intervals.data(), intervals.size() * sizeof(EpdUnicodeInterval));
    ptr += intervals.size() * sizeof(EpdUnicodeInterval);

    memcpy(ptr, kernPairs.data(), kernPairs.size() * sizeof(EpdKernPair));
    ptr += kernPairs.size() * sizeof(EpdKernPair);

    memcpy(ptr, ligPairs.data(), ligPairs.size() * sizeof(EpdLigaturePair));

    if (progress) progress(100);

    return blob;
}

EpdFontData FontRasterizer::fromBlob(const uint8_t* blobData, size_t blobSize) {
    EpdFontData result = {};
    if (blobSize < sizeof(BlobHeader)) return result;

    BlobHeader header;
    memcpy(&header, blobData, sizeof(BlobHeader));

    const uint8_t* ptr = blobData + sizeof(BlobHeader);

    result.bitmap = ptr;
    ptr += header.bitmapSize;

    result.glyph = reinterpret_cast<const EpdGlyph*>(ptr);
    ptr += header.glyphCount * sizeof(EpdGlyph);

    result.intervals = reinterpret_cast<const EpdUnicodeInterval*>(ptr);
    ptr += header.intervalCount * sizeof(EpdUnicodeInterval);

    result.intervalCount = header.intervalCount;
    result.advanceY = header.advanceY;
    result.ascender = header.ascender;
    result.descender = header.descender;
    result.is2Bit = (header.is2Bit != 0);

    if (header.kernPairCount > 0) {
        result.kernPairs = reinterpret_cast<const EpdKernPair*>(ptr);
        result.kernPairCount = header.kernPairCount;
        ptr += header.kernPairCount * sizeof(EpdKernPair);
    } else {
        result.kernPairs = nullptr;
        result.kernPairCount = 0;
    }

    if (header.ligaturePairCount > 0) {
        result.ligaturePairs = reinterpret_cast<const EpdLigaturePair*>(ptr);
        result.ligaturePairCount = header.ligaturePairCount;
    } else {
        result.ligaturePairs = nullptr;
        result.ligaturePairCount = 0;
    }

    return result;
}
