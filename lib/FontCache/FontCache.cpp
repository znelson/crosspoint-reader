#include "FontCache.h"

#include <Logging.h>

#include <cstring>

#include <esp_partition.h>
#include <esp_spi_flash.h>

static const char* TAG = "FCACHE";

FontCache::~FontCache() {
    for (auto* f : ownedFonts_) {
        delete f;
    }
    ownedFonts_.clear();
    if (mmapHandle_) {
        spi_flash_munmap(static_cast<spi_flash_mmap_handle_t>((uintptr_t)mmapHandle_));
        mmapHandle_ = nullptr;
    }
}

bool FontCache::init() {
    const esp_partition_t* part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, static_cast<esp_partition_subtype_t>(0x82), "fontcache");
    if (!part) {
        LOG_ERR(TAG, "fontcache partition not found");
        return false;
    }

    partitionSize_ = part->size;

    // Memory-map the entire partition
    const void* ptr = nullptr;
    spi_flash_mmap_handle_t handle;
    esp_err_t err = esp_partition_mmap(part, 0, part->size, SPI_FLASH_MMAP_DATA, &ptr, &handle);
    if (err != ESP_OK) {
        LOG_ERR(TAG, "Failed to mmap fontcache partition: %s", esp_err_to_name(err));
        return false;
    }

    mmapHandle_ = reinterpret_cast<const void*>((uintptr_t)handle);
    partitionPtr_ = ptr;
    mappedBase_ = static_cast<const uint8_t*>(ptr);

    initialized_ = true;
    parseHeader();
    rebuildFontData();

    LOG_DBG(TAG, "FontCache initialized with %d cached fonts, partition size %u",
            header_.fontCount, (unsigned)partitionSize_);
    return true;
}

bool FontCache::parseHeader() {
    memset(&header_, 0, sizeof(header_));
    if (!mappedBase_) return false;

    memcpy(&header_, mappedBase_, sizeof(CacheHeader));

    if (header_.magic != MAGIC || header_.version != VERSION) {
        // Empty or corrupted cache -- reset
        memset(&header_, 0, sizeof(header_));
        header_.magic = MAGIC;
        header_.version = VERSION;
        header_.fontCount = 0;
        return false;
    }

    if (header_.fontCount > MAX_FONTS) {
        header_.fontCount = MAX_FONTS;
    }

    return true;
}

void FontCache::rebuildFontData() {
    cachedFonts_.clear();
    if (!mappedBase_) return;

    cachedFonts_.reserve(header_.fontCount);
    for (int i = 0; i < header_.fontCount; i++) {
        const CacheEntry& entry = header_.entries[i];
        CachedFont cf = {};
        cf.fontId = entry.fontId;

        for (int s = 0; s < NUM_STYLES; s++) {
            if (entry.styleSize[s] > 0 && entry.styleOffset[s] > 0) {
                cf.styles[s] = FontRasterizer::fromBlob(
                    mappedBase_ + entry.styleOffset[s], entry.styleSize[s]);
                cf.valid[s] = true;
            }
        }
        cachedFonts_.push_back(cf);
    }
}

bool FontCache::hasFont(int fontId) const {
    for (const auto& cf : cachedFonts_) {
        if (cf.fontId == fontId) return true;
    }
    return false;
}

size_t FontCache::nextBlobOffset() const {
    size_t offset = HEADER_SIZE;
    for (int i = 0; i < header_.fontCount; i++) {
        for (int s = 0; s < NUM_STYLES; s++) {
            size_t end = header_.entries[i].styleOffset[s] + header_.entries[i].styleSize[s];
            if (end > offset) offset = end;
        }
    }
    return alignTo4K(offset);
}

bool FontCache::cacheFont(int fontId, const uint8_t* ttfPtrs[4], const size_t ttfSizes[4],
                          int pixelSize, ProgressCallback progress) {
    if (!initialized_) return false;

    const esp_partition_t* part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, static_cast<esp_partition_subtype_t>(0x82), "fontcache");
    if (!part) return false;

    // Check if this font already exists -- if so, evict it first (simple strategy)
    // For now, we use the simple approach: if the partition is getting full,
    // erase everything and start fresh with just this font.

    // Find or allocate slot
    int slotIdx = -1;
    for (int i = 0; i < header_.fontCount; i++) {
        if (header_.entries[i].fontId == fontId) {
            slotIdx = i;
            break;
        }
    }

    // Calculate where to start writing blobs
    size_t writeOffset = nextBlobOffset();

    // If updating an existing entry, we don't reclaim old space (simple approach).
    // If full, erase and re-cache.
    size_t maxDataOffset = partitionSize_ - STAGING_RESERVE;

    // Check if we have enough space (rough estimate: ~100KB per style)
    if (writeOffset + 4 * 200 * 1024 > maxDataOffset && header_.fontCount > 0) {
        // Erase everything
        LOG_DBG(TAG, "Partition nearly full, erasing entire cache");
        esp_err_t err = esp_partition_erase_range(part, 0, partitionSize_);
        if (err != ESP_OK) {
            LOG_ERR(TAG, "Failed to erase partition: %s", esp_err_to_name(err));
            return false;
        }
        memset(&header_, 0, sizeof(header_));
        header_.magic = MAGIC;
        header_.version = VERSION;
        header_.fontCount = 0;
        writeOffset = HEADER_SIZE;
        slotIdx = -1;
    }

    if (slotIdx < 0) {
        if (header_.fontCount >= MAX_FONTS) {
            LOG_ERR(TAG, "Too many cached fonts (max %d)", MAX_FONTS);
            return false;
        }
        slotIdx = header_.fontCount;
        header_.fontCount++;
    }

    CacheEntry& entry = header_.entries[slotIdx];
    entry.fontId = fontId;

    // Rasterize each style
    FontRasterizer rasterizer;
    for (int s = 0; s < NUM_STYLES; s++) {
        if (!ttfPtrs[s] || ttfSizes[s] == 0) {
            entry.styleOffset[s] = 0;
            entry.styleSize[s] = 0;
            continue;
        }

        auto blob = rasterizer.rasterize(ttfPtrs[s], ttfSizes[s], pixelSize,
            [&progress, s](int p) {
                if (progress) {
                    progress(s * 25 + p / 4);
                }
            });

        if (blob.empty()) {
            LOG_ERR(TAG, "Rasterization failed for style %d", s);
            entry.styleOffset[s] = 0;
            entry.styleSize[s] = 0;
            continue;
        }

        // Align write offset to 4KB boundary
        writeOffset = alignTo4K(writeOffset);

        if (writeOffset + blob.size() > maxDataOffset) {
            LOG_ERR(TAG, "Not enough space for style %d (need %u, have %u)",
                    s, (unsigned)blob.size(), (unsigned)(maxDataOffset - writeOffset));
            return false;
        }

        // Erase the flash sectors we'll write to
        size_t eraseSize = alignTo4K(blob.size());
        esp_err_t err = esp_partition_erase_range(part, writeOffset, eraseSize);
        if (err != ESP_OK) {
            LOG_ERR(TAG, "Failed to erase for style %d: %s", s, esp_err_to_name(err));
            return false;
        }

        // Write the blob
        err = esp_partition_write(part, writeOffset, blob.data(), blob.size());
        if (err != ESP_OK) {
            LOG_ERR(TAG, "Failed to write style %d: %s", s, esp_err_to_name(err));
            return false;
        }

        entry.styleOffset[s] = (uint32_t)writeOffset;
        entry.styleSize[s] = (uint32_t)blob.size();
        writeOffset += blob.size();

        LOG_DBG(TAG, "Style %d: %u bytes at offset 0x%X", s, (unsigned)blob.size(), (unsigned)entry.styleOffset[s]);
    }

    // Write header LAST for power-loss safety
    esp_err_t err = esp_partition_erase_range(part, 0, HEADER_SIZE);
    if (err != ESP_OK) {
        LOG_ERR(TAG, "Failed to erase header: %s", esp_err_to_name(err));
        return false;
    }
    err = esp_partition_write(part, 0, &header_, sizeof(CacheHeader));
    if (err != ESP_OK) {
        LOG_ERR(TAG, "Failed to write header: %s", esp_err_to_name(err));
        return false;
    }

    // Re-mmap to see the new data
    if (mmapHandle_) {
        spi_flash_munmap(static_cast<spi_flash_mmap_handle_t>((uintptr_t)mmapHandle_));
        mmapHandle_ = nullptr;
        mappedBase_ = nullptr;
    }

    const void* ptr = nullptr;
    spi_flash_mmap_handle_t handle;
    err = esp_partition_mmap(part, 0, part->size, SPI_FLASH_MMAP_DATA, &ptr, &handle);
    if (err != ESP_OK) {
        LOG_ERR(TAG, "Failed to re-mmap after cache write: %s", esp_err_to_name(err));
        return false;
    }
    mmapHandle_ = reinterpret_cast<const void*>((uintptr_t)handle);
    mappedBase_ = static_cast<const uint8_t*>(ptr);

    rebuildFontData();

    if (progress) progress(100);
    LOG_DBG(TAG, "Font %d cached successfully", fontId);
    return true;
}

const EpdFontData* FontCache::getFont(int fontId, EpdFontFamily::Style style) const {
    int styleIdx = static_cast<int>(style) & 0x03;  // Mask to 0-3
    for (const auto& cf : cachedFonts_) {
        if (cf.fontId == fontId && cf.valid[styleIdx]) {
            return &cf.styles[styleIdx];
        }
    }
    return nullptr;
}

EpdFontFamily FontCache::buildEpdFontFamily(int fontId) {
    // Find the CachedFont once instead of 4 separate linear scans via getFont()
    const CachedFont* found = nullptr;
    for (const auto& cf : cachedFonts_) {
        if (cf.fontId == fontId) {
            found = &cf;
            break;
        }
    }

    auto makeFont = [&](int styleIdx) -> EpdFont* {
        if (!found || !found->valid[styleIdx]) return nullptr;
        auto* f = new EpdFont(&found->styles[styleIdx]);
        ownedFonts_.push_back(f);
        return f;
    };

    return EpdFontFamily(makeFont(0), makeFont(1), makeFont(2), makeFont(3));
}

const uint8_t* FontCache::getStagingBase() const {
    if (!mappedBase_) return nullptr;
    size_t stagingOffset = partitionSize_ - STAGING_RESERVE;
    return mappedBase_ + stagingOffset;
}

size_t FontCache::getStagingSize() const {
    return STAGING_RESERVE;
}

const uint8_t* FontCache::stageToFlash(const uint8_t* data, size_t dataSize) {
    if (!initialized_ || !data || dataSize == 0) return nullptr;

    const esp_partition_t* part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, static_cast<esp_partition_subtype_t>(0x82), "fontcache");
    if (!part) return nullptr;

    size_t stagingOffset = partitionSize_ - STAGING_RESERVE;
    if (dataSize > STAGING_RESERVE) {
        LOG_ERR(TAG, "TTF too large for staging area: %u > %u", (unsigned)dataSize, (unsigned)STAGING_RESERVE);
        return nullptr;
    }

    // Erase staging area
    size_t eraseSize = alignTo4K(dataSize);
    esp_err_t err = esp_partition_erase_range(part, stagingOffset, eraseSize);
    if (err != ESP_OK) {
        LOG_ERR(TAG, "Failed to erase staging area: %s", esp_err_to_name(err));
        return nullptr;
    }

    // Write TTF data to staging
    err = esp_partition_write(part, stagingOffset, data, dataSize);
    if (err != ESP_OK) {
        LOG_ERR(TAG, "Failed to write to staging area: %s", esp_err_to_name(err));
        return nullptr;
    }

    // Re-mmap to see the staged data
    if (mmapHandle_) {
        spi_flash_munmap(static_cast<spi_flash_mmap_handle_t>((uintptr_t)mmapHandle_));
        mmapHandle_ = nullptr;
    }

    const void* ptr = nullptr;
    spi_flash_mmap_handle_t handle;
    err = esp_partition_mmap(part, 0, part->size, SPI_FLASH_MMAP_DATA, &ptr, &handle);
    if (err != ESP_OK) {
        LOG_ERR(TAG, "Failed to re-mmap after staging: %s", esp_err_to_name(err));
        return nullptr;
    }
    mmapHandle_ = reinterpret_cast<const void*>((uintptr_t)handle);
    mappedBase_ = static_cast<const uint8_t*>(ptr);

    return mappedBase_ + stagingOffset;
}
