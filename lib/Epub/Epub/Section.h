#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "Epub.h"

class Page;
class GfxRenderer;

class Section {
  std::shared_ptr<Epub> epub;
  const int spineIndex;
  GfxRenderer& renderer;
  std::string filePath;
  FsFile file;

  void writeSectionFileHeader(int fontId, float lineCompression, bool extraParagraphSpacing, uint8_t paragraphAlignment,
                              uint16_t viewportWidth, uint16_t viewportHeight, bool hyphenationEnabled,
                              bool embeddedStyle);
  uint32_t onPageComplete(std::unique_ptr<Page> page);

  struct TocBoundary {
    int tocIndex;
    uint16_t startPage;
  };
  std::vector<TocBoundary> tocBoundaries;
  bool tocBoundariesLoaded = false;

 public:
  uint16_t pageCount = 0;
  int currentPage = 0;

  explicit Section(const std::shared_ptr<Epub>& epub, const int spineIndex, GfxRenderer& renderer)
      : epub(epub),
        spineIndex(spineIndex),
        renderer(renderer),
        filePath(epub->getCachePath() + "/sections/" + std::to_string(spineIndex) + ".bin") {}
  ~Section() = default;
  bool loadSectionFile(int fontId, float lineCompression, bool extraParagraphSpacing, uint8_t paragraphAlignment,
                       uint16_t viewportWidth, uint16_t viewportHeight, bool hyphenationEnabled, bool embeddedStyle);
  bool clearCache() const;
  bool createSectionFile(int fontId, float lineCompression, bool extraParagraphSpacing, uint8_t paragraphAlignment,
                         uint16_t viewportWidth, uint16_t viewportHeight, bool hyphenationEnabled, bool embeddedStyle,
                         const std::function<void()>& popupFn = nullptr);
  std::unique_ptr<Page> loadPageFromSectionFile();

  // Look up which page an anchor (HTML id) maps to. Returns -1 if not found.
  static int getPageForAnchor(const std::string& cachePath, int spineIndex, const std::string& anchor);

  // Build TOC boundary data for this spine (call once after section is loaded).
  void loadTocBoundaries();
  // Given a page in this section, return the TOC index for that page.
  int getTocIndexForPage(int page) const;
};
