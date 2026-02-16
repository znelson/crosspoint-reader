#include "OpenTypeKernLig.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace opentype {

// --- Big-endian readers at arbitrary byte offsets ---
static inline uint16_t u16(const uint8_t* p) { return (uint16_t(p[0]) << 8) | p[1]; }
static inline int16_t s16(const uint8_t* p) { return int16_t(u16(p)); }
static inline uint32_t u32(const uint8_t* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }

// --- Locate an OpenType table by its 4-byte tag ---
static const uint8_t* findTable(const uint8_t* font, size_t fontLen, const char* tag) {
    if (fontLen < 12) return nullptr;
    uint16_t numTables = u16(font + 4);
    if (12 + numTables * 16 > fontLen) return nullptr;
    for (uint16_t i = 0; i < numTables; i++) {
        const uint8_t* rec = font + 12 + i * 16;
        if (memcmp(rec, tag, 4) == 0) {
            uint32_t offset = u32(rec + 8);
            uint32_t length = u32(rec + 12);
            if (offset + length <= fontLen) return font + offset;
            return nullptr;
        }
    }
    return nullptr;
}

// --- Build both forward (cp -> glyph) and reverse (glyph -> first cp) cmap in one pass ---
struct CmapResult {
    std::unordered_map<uint16_t, uint32_t> glyphToCp;
    std::unordered_map<uint32_t, uint16_t> cpToGlyph;
};

static CmapResult buildCmaps(const uint8_t* font, size_t fontLen) {
    CmapResult result;
    const uint8_t* cmap = findTable(font, fontLen, "cmap");
    if (!cmap) return result;

    uint16_t numSubtables = u16(cmap + 2);
    for (uint16_t i = 0; i < numSubtables; i++) {
        const uint8_t* rec = cmap + 4 + i * 8;
        uint16_t platformID = u16(rec);
        uint16_t encodingID = u16(rec + 2);
        uint32_t offset = u32(rec + 4);

        if (!((platformID == 0) || (platformID == 3 && (encodingID == 1 || encodingID == 10)))) continue;

        const uint8_t* sub = cmap + offset;
        uint16_t format = u16(sub);

        if (format == 4) {
            uint16_t segCount = u16(sub + 6) / 2;
            const uint8_t* endCodes = sub + 14;
            const uint8_t* startCodes = endCodes + segCount * 2 + 2;
            const uint8_t* idDeltas = startCodes + segCount * 2;
            const uint8_t* idRangeOffsets = idDeltas + segCount * 2;

            for (uint16_t seg = 0; seg < segCount; seg++) {
                uint16_t endCode = u16(endCodes + seg * 2);
                uint16_t startCode = u16(startCodes + seg * 2);
                int16_t idDelta = s16(idDeltas + seg * 2);
                uint16_t idRangeOffset = u16(idRangeOffsets + seg * 2);

                if (startCode == 0xFFFF) break;

                for (uint32_t cp = startCode; cp <= endCode; cp++) {
                    uint16_t gid;
                    if (idRangeOffset == 0) {
                        gid = (uint16_t)((cp + idDelta) & 0xFFFF);
                    } else {
                        const uint8_t* ptr = idRangeOffsets + seg * 2 + idRangeOffset + (cp - startCode) * 2;
                        gid = u16(ptr);
                        if (gid != 0) gid = (uint16_t)((gid + idDelta) & 0xFFFF);
                    }
                    if (gid != 0) {
                        result.cpToGlyph[cp] = gid;
                        result.glyphToCp.emplace(gid, cp);
                    }
                }
            }
            break;
        } else if (format == 12) {
            uint32_t numGroups = u32(sub + 12);
            const uint8_t* groups = sub + 16;
            for (uint32_t g = 0; g < numGroups; g++) {
                uint32_t startCharCode = u32(groups + g * 12);
                uint32_t endCharCode = u32(groups + g * 12 + 4);
                uint32_t startGlyphID = u32(groups + g * 12 + 8);
                for (uint32_t cp = startCharCode; cp <= endCharCode; cp++) {
                    uint16_t gid = (uint16_t)(startGlyphID + (cp - startCharCode));
                    if (gid != 0) {
                        result.cpToGlyph[cp] = gid;
                        result.glyphToCp.emplace(gid, cp);
                    }
                }
            }
            break;
        }
    }
    return result;
}

// --- Coverage table lookup ---
// Returns the coverage index for glyphID, or -1 if not covered.
static int coverageLookup(const uint8_t* coverage, uint16_t glyphID) {
    uint16_t format = u16(coverage);
    if (format == 1) {
        uint16_t count = u16(coverage + 2);
        // Binary search over sorted glyph array
        int lo = 0, hi = count - 1;
        while (lo <= hi) {
            int mid = (lo + hi) / 2;
            uint16_t gid = u16(coverage + 4 + mid * 2);
            if (gid == glyphID) return mid;
            if (gid < glyphID) lo = mid + 1;
            else hi = mid - 1;
        }
    } else if (format == 2) {
        uint16_t rangeCount = u16(coverage + 2);
        int lo = 0, hi = rangeCount - 1;
        while (lo <= hi) {
            int mid = (lo + hi) / 2;
            const uint8_t* rr = coverage + 4 + mid * 6;
            uint16_t startGlyph = u16(rr);
            uint16_t endGlyph = u16(rr + 2);
            if (glyphID < startGlyph) {
                hi = mid - 1;
            } else if (glyphID > endGlyph) {
                lo = mid + 1;
            } else {
                return u16(rr + 4) + (glyphID - startGlyph);
            }
        }
    }
    return -1;
}

// --- ClassDef table parsing ---
// Returns the class value for glyphID, or 0 if not classified.
static uint16_t classDefLookup(const uint8_t* classDef, uint16_t glyphID) {
    uint16_t format = u16(classDef);
    if (format == 1) {
        uint16_t startGlyph = u16(classDef + 2);
        uint16_t count = u16(classDef + 4);
        if (glyphID >= startGlyph && glyphID < startGlyph + count) {
            return u16(classDef + 6 + (glyphID - startGlyph) * 2);
        }
    } else if (format == 2) {
        uint16_t rangeCount = u16(classDef + 2);
        int lo = 0, hi = rangeCount - 1;
        while (lo <= hi) {
            int mid = (lo + hi) / 2;
            const uint8_t* rr = classDef + 4 + mid * 6;
            uint16_t startGlyph = u16(rr);
            uint16_t endGlyph = u16(rr + 2);
            if (glyphID < startGlyph) {
                hi = mid - 1;
            } else if (glyphID > endGlyph) {
                lo = mid + 1;
            } else {
                return u16(rr + 4);
            }
        }
    }
    return 0;
}

// --- Extract XAdvance from a ValueRecord ---
// valueFormat is a bitmask telling which fields are present.
// We only care about XAdvance (bit 2, 0x0004).
static int16_t readXAdvance(const uint8_t* vr, uint16_t valueFormat) {
    int offset = 0;
    if (valueFormat & 0x0001) offset += 2;  // XPlacement
    if (valueFormat & 0x0002) offset += 2;  // YPlacement
    if (valueFormat & 0x0004) return s16(vr + offset);  // XAdvance
    return 0;
}

static int valueRecordSize(uint16_t valueFormat) {
    int size = 0;
    for (int i = 0; i < 8; i++) {
        if (valueFormat & (1 << i)) size += 2;
    }
    return size;
}

// --- GPOS PairPos subtable extraction ---
static void extractPairPosFormat1(const uint8_t* subtable, uint16_t valueFormat1, uint16_t valueFormat2,
                                  const std::unordered_map<uint16_t, uint32_t>& glyphToCp,
                                  const std::unordered_set<uint32_t>& glyphSetLookup,
                                  std::unordered_map<uint32_t, int16_t>& rawKern) {
    const uint8_t* coverage = subtable + u16(subtable + 2);
    uint16_t pairSetCount = u16(subtable + 8);
    int vr1Size = valueRecordSize(valueFormat1);
    int vr2Size = valueRecordSize(valueFormat2);

    for (uint16_t i = 0; i < pairSetCount; i++) {
        uint16_t pairSetOffset = u16(subtable + 10 + i * 2);
        const uint8_t* pairSet = subtable + pairSetOffset;
        uint16_t pairValueCount = u16(pairSet);
        int recordSize = 2 + vr1Size + vr2Size;

        // The i-th PairSet corresponds to the i-th glyph in the coverage table.
        // Reconstruct firstGlyph from coverage.
        uint16_t covFormat = u16(coverage);
        uint16_t firstGlyph = 0;
        if (covFormat == 1) {
            firstGlyph = u16(coverage + 4 + i * 2);
        } else if (covFormat == 2) {
            // Walk range records to find the i-th coverage index
            uint16_t rangeCount = u16(coverage + 2);
            int idx = 0;
            bool found = false;
            for (uint16_t r = 0; r < rangeCount && !found; r++) {
                const uint8_t* rr = coverage + 4 + r * 6;
                uint16_t startGlyph = u16(rr);
                uint16_t endGlyph = u16(rr + 2);
                uint16_t startIdx = u16(rr + 4);
                if ((int)i >= startIdx && (int)i <= startIdx + (endGlyph - startGlyph)) {
                    firstGlyph = startGlyph + (i - startIdx);
                    found = true;
                }
            }
            if (!found) continue;
        }

        auto fIt = glyphToCp.find(firstGlyph);
        if (fIt == glyphToCp.end()) continue;
        if (glyphSetLookup.count(fIt->second) == 0) continue;

        for (uint16_t j = 0; j < pairValueCount; j++) {
            const uint8_t* pvr = pairSet + 2 + j * recordSize;
            uint16_t secondGlyph = u16(pvr);
            int16_t xAdv = readXAdvance(pvr + 2, valueFormat1);

            if (xAdv == 0) continue;
            auto sIt = glyphToCp.find(secondGlyph);
            if (sIt == glyphToCp.end()) continue;
            if (glyphSetLookup.count(sIt->second) == 0) continue;

            uint32_t key = (uint32_t(firstGlyph) << 16) | secondGlyph;
            auto it = rawKern.find(key);
            if (it == rawKern.end() || std::abs(xAdv) > std::abs(it->second)) {
                rawKern[key] = xAdv;
            }
        }
    }
}

static void extractPairPosFormat2(const uint8_t* subtable, uint16_t valueFormat1, uint16_t valueFormat2,
                                  const std::unordered_map<uint16_t, uint32_t>& glyphToCp,
                                  const std::unordered_map<uint32_t, uint16_t>& cpToGlyph,
                                  const std::unordered_set<uint32_t>& glyphSetLookup,
                                  std::unordered_map<uint32_t, int16_t>& rawKern) {
    const uint8_t* classDef1 = subtable + u16(subtable + 8);
    const uint8_t* classDef2 = subtable + u16(subtable + 10);
    uint16_t class1Count = u16(subtable + 12);
    uint16_t class2Count = u16(subtable + 14);
    int vr1Size = valueRecordSize(valueFormat1);
    int vr2Size = valueRecordSize(valueFormat2);
    int recordSize = vr1Size + vr2Size;

    // Build class -> glyph list for classes that contain glyphs in our set
    std::unordered_map<uint16_t, std::vector<uint16_t>> class1Glyphs, class2Glyphs;
    for (const auto& cpGid : cpToGlyph) {
        if (glyphSetLookup.count(cpGid.first) == 0) continue;
        uint16_t c1 = classDefLookup(classDef1, cpGid.second);
        uint16_t c2 = classDefLookup(classDef2, cpGid.second);
        class1Glyphs[c1].push_back(cpGid.second);
        class2Glyphs[c2].push_back(cpGid.second);
    }

    const uint8_t* classRecords = subtable + 16;
    for (uint16_t c1 = 0; c1 < class1Count; c1++) {
        auto c1It = class1Glyphs.find(c1);
        if (c1It == class1Glyphs.end()) continue;

        for (uint16_t c2 = 0; c2 < class2Count; c2++) {
            const uint8_t* vrPtr = classRecords + (c1 * class2Count + c2) * recordSize;
            int16_t xAdv = readXAdvance(vrPtr, valueFormat1);
            if (xAdv == 0) continue;

            auto c2It = class2Glyphs.find(c2);
            if (c2It == class2Glyphs.end()) continue;

            for (uint16_t g1 : c1It->second) {
                for (uint16_t g2 : c2It->second) {
                    uint32_t key = (uint32_t(g1) << 16) | g2;
                    auto it = rawKern.find(key);
                    if (it == rawKern.end() || std::abs(xAdv) > std::abs(it->second)) {
                        rawKern[key] = xAdv;
                    }
                }
            }
        }
    }
}

// ====================================================================
// Public API: extractKerning
// ====================================================================

std::vector<EpdKernPair> extractKerning(const uint8_t* fontData, size_t fontDataSize, float ppem,
                                        const uint32_t* glyphSet, size_t glyphSetSize) {
    std::unordered_set<uint32_t> glyphSetLookup(glyphSet, glyphSet + glyphSetSize);
    auto cmaps = buildCmaps(fontData, fontDataSize);
    auto& glyphToCp = cmaps.glyphToCp;
    auto& cpToGlyph = cmaps.cpToGlyph;

    // Get units_per_em from head table
    const uint8_t* head = findTable(fontData, fontDataSize, "head");
    if (!head) return {};
    uint16_t unitsPerEm = u16(head + 18);
    if (unitsPerEm == 0) return {};

    float scale = ppem / (float)unitsPerEm;

    // Collect raw kerning in design units: packed (glyphID_left << 16 | glyphID_right) -> du
    std::unordered_map<uint32_t, int16_t> rawKern;

    const uint8_t* gpos = findTable(fontData, fontDataSize, "GPOS");
    if (gpos) {
        // Parse GPOS ScriptList / FeatureList / LookupList
        uint16_t featureListOffset = u16(gpos + 6);
        uint16_t lookupListOffset = u16(gpos + 8);
        const uint8_t* featureList = gpos + featureListOffset;
        const uint8_t* lookupList = gpos + lookupListOffset;

        // Find 'kern' feature lookup indices
        std::set<uint16_t> kernLookupIndices;
        uint16_t featureCount = u16(featureList);
        for (uint16_t i = 0; i < featureCount; i++) {
            const uint8_t* fr = featureList + 2 + i * 6;
            if (memcmp(fr, "kern", 4) == 0) {
                uint16_t featureOffset = u16(fr + 4);
                const uint8_t* feature = featureList + featureOffset;
                uint16_t lookupCount = u16(feature + 2);
                for (uint16_t j = 0; j < lookupCount; j++) {
                    kernLookupIndices.insert(u16(feature + 4 + j * 2));
                }
            }
        }

        uint16_t lookupCount = u16(lookupList);
        for (uint16_t li : kernLookupIndices) {
            if (li >= lookupCount) continue;
            uint16_t lookupOffset = u16(lookupList + 2 + li * 2);
            const uint8_t* lookup = lookupList + lookupOffset;
            uint16_t lookupType = u16(lookup);
            uint16_t subTableCount = u16(lookup + 4);

            for (uint16_t si = 0; si < subTableCount; si++) {
                uint16_t subOffset = u16(lookup + 6 + si * 2);
                const uint8_t* subtable = lookup + subOffset;
                uint16_t actualType = lookupType;

                // Handle Extension wrappers (type 9)
                if (lookupType == 9) {
                    actualType = u16(subtable + 2);
                    uint32_t extOffset = u32(subtable + 4);
                    subtable = subtable + extOffset;
                    // Re-read after resolving extension: subtable now points to the real one
                    // Note: extension points from start of extension subtable, not from lookup
                    subtable = lookup + subOffset + extOffset;
                }

                // PairPos (type 2)
                if (actualType != 2) continue;

                uint16_t format = u16(subtable);
                uint16_t valueFormat1 = u16(subtable + 4);
                uint16_t valueFormat2 = u16(subtable + 6);

                if (format == 1) {
                    extractPairPosFormat1(subtable, valueFormat1, valueFormat2, glyphToCp, glyphSetLookup, rawKern);
                } else if (format == 2) {
                    extractPairPosFormat2(subtable, valueFormat1, valueFormat2, glyphToCp, cpToGlyph, glyphSetLookup, rawKern);
                }
            }
        }
    }

    // If no GPOS kerning found, try legacy kern table via stb_truetype
    // (handled by caller via stbtt_GetGlyphKernAdvance as a fallback)

    // Scale to pixels and pack
    std::vector<EpdKernPair> pairs;
    pairs.reserve(rawKern.size());
    for (const auto& kernEntry : rawKern) {
        auto lIt = glyphToCp.find((uint16_t)(kernEntry.first >> 16));
        auto rIt = glyphToCp.find((uint16_t)(kernEntry.first & 0xFFFF));
        if (lIt == glyphToCp.end() || rIt == glyphToCp.end()) continue;

        int adjust = (int)std::floor(kernEntry.second * scale);
        if (adjust == 0) continue;
        if (adjust < -128) adjust = -128;
        if (adjust > 127) adjust = 127;

        uint32_t packed = (lIt->second << 16) | (rIt->second & 0xFFFF);
        pairs.push_back({packed, (int8_t)adjust});
    }

    std::sort(pairs.begin(), pairs.end(), [](const EpdKernPair& a, const EpdKernPair& b) {
        return a.pair < b.pair;
    });
    return pairs;
}

// ====================================================================
// Standard Unicode ligature codepoints for known input sequences
// ====================================================================

struct StandardLigature {
    uint32_t seq[3];
    uint8_t seqLen;
    uint32_t ligCp;
};

static const StandardLigature STANDARD_LIGATURES[] = {
    {{0x66, 0x66, 0},       2, 0xFB00},  // ff
    {{0x66, 0x69, 0},       2, 0xFB01},  // fi
    {{0x66, 0x6C, 0},       2, 0xFB02},  // fl
    {{0x66, 0x66, 0x69},    3, 0xFB03},  // ffi
    {{0x66, 0x66, 0x6C},    3, 0xFB04},  // ffl
    {{0x17F, 0x74, 0},      2, 0xFB05},  // long-s + t
    {{0x73, 0x74, 0},       2, 0xFB06},  // st
};

static uint32_t findStandardLigature(const uint32_t* seq, uint8_t seqLen) {
    for (const auto& sl : STANDARD_LIGATURES) {
        if (sl.seqLen != seqLen) continue;
        bool match = true;
        for (uint8_t i = 0; i < seqLen; i++) {
            if (sl.seq[i] != seq[i]) { match = false; break; }
        }
        if (match) return sl.ligCp;
    }
    return 0;
}

// ====================================================================
// Public API: extractLigatures
// ====================================================================

std::vector<EpdLigaturePair> extractLigatures(const uint8_t* fontData, size_t fontDataSize,
                                              const uint32_t* glyphSet, size_t glyphSetSize) {
    std::unordered_set<uint32_t> glyphSetLookup(glyphSet, glyphSet + glyphSetSize);
    auto cmaps = buildCmaps(fontData, fontDataSize);
    auto& glyphToCp = cmaps.glyphToCp;

    const uint8_t* gsub = findTable(fontData, fontDataSize, "GSUB");
    if (!gsub) return {};

    uint16_t featureListOffset = u16(gsub + 6);
    uint16_t lookupListOffset = u16(gsub + 8);
    const uint8_t* featureList = gsub + featureListOffset;
    const uint8_t* lookupList = gsub + lookupListOffset;

    // Find liga/rlig feature lookup indices
    std::set<uint16_t> ligaLookupIndices;
    uint16_t featureCount = u16(featureList);
    for (uint16_t i = 0; i < featureCount; i++) {
        const uint8_t* fr = featureList + 2 + i * 6;
        if (memcmp(fr, "liga", 4) == 0 || memcmp(fr, "rlig", 4) == 0) {
            uint16_t featureOffset = u16(fr + 4);
            const uint8_t* feature = featureList + featureOffset;
            uint16_t lookupCount = u16(feature + 2);
            for (uint16_t j = 0; j < lookupCount; j++) {
                ligaLookupIndices.insert(u16(feature + 4 + j * 2));
            }
        }
    }

    // Collect raw ligatures: (codepoint_sequence) -> ligature_codepoint
    std::map<std::vector<uint32_t>, uint32_t> rawLigatures;

    uint16_t lookupCount = u16(lookupList);
    for (uint16_t li : ligaLookupIndices) {
        if (li >= lookupCount) continue;
        uint16_t lookupOffset = u16(lookupList + 2 + li * 2);
        const uint8_t* lookup = lookupList + lookupOffset;
        uint16_t lookupType = u16(lookup);
        uint16_t subTableCount = u16(lookup + 4);

        for (uint16_t si = 0; si < subTableCount; si++) {
            uint16_t subOffset = u16(lookup + 6 + si * 2);
            const uint8_t* subtable = lookup + subOffset;
            uint16_t actualType = lookupType;

            // Handle Extension wrappers (type 7)
            if (lookupType == 7) {
                actualType = u16(subtable + 2);
                uint32_t extOffset = u32(subtable + 4);
                subtable = lookup + subOffset + extOffset;
            }

            // LigatureSubst (type 4)
            if (actualType != 4) continue;

            uint16_t format = u16(subtable);
            if (format != 1) continue;

            const uint8_t* coverage = subtable + u16(subtable + 2);
            uint16_t ligSetCount = u16(subtable + 4);

            for (uint16_t ls = 0; ls < ligSetCount; ls++) {
                // Find the first glyph from coverage index
                uint16_t covFormat = u16(coverage);
                uint16_t firstGlyph = 0;
                if (covFormat == 1) {
                    firstGlyph = u16(coverage + 4 + ls * 2);
                } else if (covFormat == 2) {
                    uint16_t rangeCount = u16(coverage + 2);
                    int idx = 0;
                    bool found = false;
                    for (uint16_t r = 0; r < rangeCount && !found; r++) {
                        const uint8_t* rr = coverage + 4 + r * 6;
                        uint16_t startGlyph = u16(rr);
                        uint16_t endGlyph = u16(rr + 2);
                        uint16_t startIdx = u16(rr + 4);
                        if ((int)ls >= startIdx && (int)ls <= startIdx + (endGlyph - startGlyph)) {
                            firstGlyph = startGlyph + (ls - startIdx);
                            found = true;
                        }
                    }
                    if (!found) continue;
                }

                auto fIt = glyphToCp.find(firstGlyph);
                if (fIt == glyphToCp.end()) continue;

                uint16_t ligSetOffset = u16(subtable + 6 + ls * 2);
                const uint8_t* ligSet = subtable + ligSetOffset;
                uint16_t ligCount = u16(ligSet);

                for (uint16_t l = 0; l < ligCount; l++) {
                    uint16_t ligOffset = u16(ligSet + 2 + l * 2);
                    const uint8_t* lig = ligSet + ligOffset;
                    uint16_t ligGlyph = u16(lig);
                    uint16_t compCount = u16(lig + 2);
                    // compCount includes the first glyph, so components = compCount - 1

                    std::vector<uint32_t> seq;
                    seq.push_back(fIt->second);
                    bool valid = true;
                    for (uint16_t c = 0; c < compCount - 1; c++) {
                        uint16_t compGlyph = u16(lig + 4 + c * 2);
                        auto cIt = glyphToCp.find(compGlyph);
                        if (cIt == glyphToCp.end()) { valid = false; break; }
                        seq.push_back(cIt->second);
                    }
                    if (!valid) continue;

                    auto ligCpIt = glyphToCp.find(ligGlyph);
                    uint32_t ligCp;
                    if (ligCpIt != glyphToCp.end()) {
                        ligCp = ligCpIt->second;
                    } else {
                        // Fallback to standard Unicode ligature codepoints
                        ligCp = findStandardLigature(seq.data(), (uint8_t)seq.size());
                        if (ligCp == 0) continue;
                    }

                    rawLigatures[seq] = ligCp;
                }
            }
        }
    }

    // Filter: keep only ligatures where all input and output codepoints are in glyphSet
    std::map<std::vector<uint32_t>, uint32_t> filtered;
    for (const auto& ligEntry : rawLigatures) {
        if (glyphSetLookup.count(ligEntry.second) == 0) continue;
        bool allIn = true;
        for (size_t i = 0; i < ligEntry.first.size(); i++) {
            if (glyphSetLookup.count(ligEntry.first[i]) == 0) { allIn = false; break; }
        }
        if (allIn) filtered[ligEntry.first] = ligEntry.second;
    }

    // Decompose into chained pairs
    std::vector<EpdLigaturePair> pairs;

    // 2-codepoint ligatures: direct pairs
    for (const auto& filtEntry : filtered) {
        if (filtEntry.first.size() == 2) {
            uint32_t packed = (filtEntry.first[0] << 16) | (filtEntry.first[1] & 0xFFFF);
            pairs.push_back({packed, filtEntry.second});
        }
    }

    // 3+ codepoint ligatures: chain through intermediates
    for (const auto& filtEntry : filtered) {
        if (filtEntry.first.size() < 3) continue;
        std::vector<uint32_t> prefix(filtEntry.first.begin(), filtEntry.first.end() - 1);
        auto prefixIt = filtered.find(prefix);
        if (prefixIt != filtered.end()) {
            uint32_t intermediateCp = prefixIt->second;
            uint32_t lastCp = filtEntry.first.back();
            uint32_t packed = (intermediateCp << 16) | (lastCp & 0xFFFF);
            pairs.push_back({packed, filtEntry.second});
        }
    }

    // Deduplicate and sort
    std::sort(pairs.begin(), pairs.end(), [](const EpdLigaturePair& a, const EpdLigaturePair& b) {
        return a.pair < b.pair;
    });
    auto last = std::unique(pairs.begin(), pairs.end(),
                            [](const EpdLigaturePair& a, const EpdLigaturePair& b) { return a.pair == b.pair; });
    pairs.erase(last, pairs.end());

    return pairs;
}

}  // namespace opentype
