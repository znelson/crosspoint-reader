#pragma once

#include <EpdFontData.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

/// Wraps stb_truetype to rasterize a TTF file at a given pixel size into
/// the serialized blob format expected by EpdFontData / FontCache.
///
/// The blob layout produced by rasterize():
///   Header (fixed):
///     uint32_t bitmapSize
///     uint32_t glyphCount
///     uint32_t intervalCount
///     uint32_t kernPairCount
///     uint32_t ligaturePairCount
///     uint8_t  advanceY
///     int32_t  ascender
///     int32_t  descender
///     uint8_t  is2Bit
///   Data (variable):
///     uint8_t  bitmap[bitmapSize]
///     EpdGlyph glyphs[glyphCount]
///     EpdUnicodeInterval intervals[intervalCount]
///     EpdKernPair kernPairs[kernPairCount]
///     EpdLigaturePair ligaturePairs[ligaturePairCount]
///
/// After writing this blob to flash and mmapping it, FontCache reconstructs
/// an EpdFontData by pointing into the mmapped region.

class FontRasterizer {
 public:
  /// Progress callback: receives a value 0..100.
  using ProgressCallback = std::function<void(int)>;

  /// Rasterize a single style of a TTF font at the given pixel size.
  /// @param fontData       Pointer to the raw TTF file data (must remain valid during call).
  /// @param fontDataSize   Size of fontData in bytes.
  /// @param pixelSize      Target font size in pixels (same as the size arg to fontconvert.py).
  /// @param progress       Optional callback for reporting progress (0-100).
  /// @return               Serialized blob ready for writing to flash, or empty on error.
  std::vector<uint8_t> rasterize(const uint8_t* fontData, size_t fontDataSize, int pixelSize,
                                 ProgressCallback progress = nullptr);

  /// Header layout at the start of a serialized blob.
  struct BlobHeader {
    uint32_t bitmapSize;
    uint32_t glyphCount;
    uint32_t intervalCount;
    uint32_t kernPairCount;
    uint32_t ligaturePairCount;
    uint8_t advanceY;
    int32_t ascender;
    int32_t descender;
    uint8_t is2Bit;
  } __attribute__((packed));

  /// Reconstruct an EpdFontData from a memory-mapped blob.
  /// The returned struct's pointers point directly into `blobData`.
  /// The caller must ensure `blobData` remains valid as long as the EpdFontData is in use.
  static EpdFontData fromBlob(const uint8_t* blobData, size_t blobSize);
};
