#pragma once

#include <EpdFont.h>
#include <EpdFontData.h>
#include <EpdFontFamily.h>

#include <cstdint>
#include <functional>
#include <map>
#include <vector>

#include "FontRasterizer.h"

/// Manages the raw flash partition used to cache rasterized font data.
///
/// Partition layout:
///   [0x000 - 0xFFF]  Header (4KB, written LAST for power-loss safety)
///     magic: 0x464F4E54 ('FONT')
///     version: 1
///     fontCount: N
///     entries[N]: { fontId:i32, styleOffset[4]:u32, styleSize[4]:u32 }
///
///   [0x1000+]  Font style blobs (each 4KB-aligned)
///     Each blob is the serialized output of FontRasterizer::rasterize().
///
/// After writing, the partition is memory-mapped via esp_partition_mmap().
/// EpdFontData structs are reconstructed with pointers into the mapped region.

class FontCache {
 public:
  /// Progress callback for rasterization: receives a value 0..100.
  using ProgressCallback = std::function<void(int)>;

  FontCache() = default;
  ~FontCache();

  /// Initialize: find the fontcache partition, mmap it, parse the header.
  /// Returns true if the partition was found (cache may be empty).
  bool init();

  /// Check if a font ID is already cached.
  bool hasFont(int fontId) const;

  /// Rasterize and cache a font family (4 styles).
  /// @param fontId        The font ID to cache.
  /// @param ttfPtrs       Array of 4 TTF data pointers [regular, bold, italic, bolditalic].
  /// @param ttfSizes      Array of 4 TTF data sizes.
  /// @param pixelSize     Target font size in pixels.
  /// @param progress      Optional progress callback.
  /// @return true on success.
  bool cacheFont(int fontId, const uint8_t* ttfPtrs[4], const size_t ttfSizes[4],
                 int pixelSize, ProgressCallback progress = nullptr);

  /// Get a pointer to the EpdFontData for a cached style, or nullptr if not cached.
  const EpdFontData* getFont(int fontId, EpdFontFamily::Style style) const;

  /// Build an EpdFontFamily from cached data.
  /// Returns an EpdFontFamily with pointers into the mmap'd region.
  /// The EpdFont objects are heap-allocated and owned by this FontCache.
  EpdFontFamily buildEpdFontFamily(int fontId);

  /// Get the base pointer to the partition data region that is usable for staging.
  /// Returns nullptr if not initialized.
  const uint8_t* getStagingBase() const;

  /// Get the size of the staging area at the end of the partition.
  size_t getStagingSize() const;

  /// Write data to a staging area at the end of the partition for TTF loading.
  /// Returns a pointer to the mmap'd staging area, or nullptr on failure.
  const uint8_t* stageToFlash(const uint8_t* data, size_t dataSize);

  static constexpr uint32_t MAGIC = 0x464F4E54;  // 'FONT'
  static constexpr uint8_t VERSION = 1;
  static constexpr size_t HEADER_SIZE = 4096;
  static constexpr size_t SECTOR_SIZE = 4096;
  static constexpr int MAX_FONTS = 16;
  static constexpr int NUM_STYLES = 4;

  // Reserve the last 1MB of the partition for TTF staging
  static constexpr size_t STAGING_RESERVE = 1024 * 1024;

 private:
  struct CacheEntry {
    int32_t fontId;
    uint32_t styleOffset[NUM_STYLES];
    uint32_t styleSize[NUM_STYLES];
  };

  struct CacheHeader {
    uint32_t magic;
    uint8_t version;
    uint8_t fontCount;
    uint8_t reserved[2];
    CacheEntry entries[MAX_FONTS];
  };

#ifdef ESP_PLATFORM
  const void* mmapHandle_ = nullptr;
  const void* partitionPtr_ = nullptr;
#endif
  const uint8_t* mappedBase_ = nullptr;
  size_t partitionSize_ = 0;
  bool initialized_ = false;

  CacheHeader header_ = {};

  // Decoded EpdFontData from the current mmap (populated in init/after cacheFont)
  struct CachedFont {
    int fontId;
    EpdFontData styles[NUM_STYLES];
    bool valid[NUM_STYLES];
  };
  std::vector<CachedFont> cachedFonts_;

  // Heap-allocated EpdFont objects (owned by us, referenced by EpdFontFamily)
  std::vector<EpdFont*> ownedFonts_;

  bool parseHeader();
  void rebuildFontData();
  size_t alignTo4K(size_t val) const { return (val + SECTOR_SIZE - 1) & ~(SECTOR_SIZE - 1); }
  size_t nextBlobOffset() const;
};
