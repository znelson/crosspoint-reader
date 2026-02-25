#ifdef BENCHMARK_MODE

#include "EpubBenchmarkDriver.h"

#include <Logging.h>

#include "activities/reader/EpubReaderActivity.h"

EpubBenchmarkDriver::EpubBenchmarkDriver(EpubReaderActivity& reader) : reader(reader) {}

bool EpubBenchmarkDriver::isWarmRender() const {
  return phase == Phase::WARM_RENDER || phase == Phase::CHAPTER_JUMPS || phase == Phase::WARM_REOPEN_WAIT;
}

void EpubBenchmarkDriver::logSdIoCounters(const char* label) {
  auto c = Storage.getSdIoCounters();
  LOG_INF("BENCH",
          "[SD_IO] %s: read_opens=%lu write_opens=%lu exists=%lu removes=%lu read_time=%lu us write_time=%lu us", label,
          static_cast<unsigned long>(c.readOpens), static_cast<unsigned long>(c.writeOpens),
          static_cast<unsigned long>(c.existsCalls), static_cast<unsigned long>(c.removeCalls),
          static_cast<unsigned long>(c.readTimeUs), static_cast<unsigned long>(c.writeTimeUs));
  Storage.resetSdIoCounters();
}

void EpubBenchmarkDriver::logPhaseEnd(const char* label) {
  BenchmarkAllocTracker::logHeapSnapshot(label);
  logSdIoCounters(label);
  LOG_INF("BENCH", "[WALL_TIME] %s: %lu ms", label, millis() - phaseStartMs);
}

void EpubBenchmarkDriver::start() {
  LOG_INF("BENCH", "=== BENCHMARK START ===");
  LOG_INF("BENCH", "Book: %s", reader.epub->getPath().c_str());
  LOG_INF("BENCH", "Spine items: %d, Page turns: %d", reader.epub->getSpineItemsCount(),
          BenchmarkState::pageTurnCount);
  BenchmarkAllocTracker::logHeapSnapshot("initial");
  Storage.resetSdIoCounters();

  startMs = millis();
  BenchmarkState::benchmarkStartMs = startMs;
  phaseStartMs = startMs;

  {
    EpubReaderActivity::RenderLock lock(reader);
    reader.section.reset();
  }
  reader.epub->clearCache();
  reader.epub->load();
  logPhaseEnd("indexing");

  phase = Phase::COLD_PARSE;
  phaseStartMs = millis();
  reader.currentSpineIndex = 0;
  reader.nextPageNumber = 0;
  totalSections = 0;
  totalPages = 0;

  LOG_INF("BENCH", "=== PHASE 1: COLD PARSE ===");
  reader.requestUpdate();
}

void EpubBenchmarkDriver::startWarmReopen() {
  LOG_INF("BENCH", "=== PHASE 4: WARM RE-OPEN ===");
  BenchmarkState::pendingReopen = false;
  Storage.resetSdIoCounters();
  phaseStartMs = millis();
  phase = Phase::WARM_REOPEN_WAIT;
  reader.requestUpdate();
}

void EpubBenchmarkDriver::loop() {
  switch (phase) {
    case Phase::COLD_PARSE:
      loopColdParse();
      return;
    case Phase::WARM_RENDER:
      loopWarmRender();
      return;
    case Phase::CHAPTER_JUMPS:
      loopChapterJumps();
      return;
    case Phase::WARM_REOPEN_WAIT:
      loopWarmReopenWait();
      return;
    case Phase::DONE:
      finish();
      return;
  }
}

void EpubBenchmarkDriver::loopColdParse() {
  int pageCount;
  {
    EpubReaderActivity::RenderLock lock(reader);
    if (!reader.section) {
      return;
    }
    pageCount = reader.section->pageCount;
  }

  totalSections++;
  totalPages += pageCount;
  LOG_INF("BENCH", "[PHASE1] Spine %d/%d done: %d pages", reader.currentSpineIndex + 1,
          reader.epub->getSpineItemsCount(), pageCount);

  if (reader.currentSpineIndex + 1 < reader.epub->getSpineItemsCount()) {
    {
      EpubReaderActivity::RenderLock lock(reader);
      reader.currentSpineIndex++;
      reader.nextPageNumber = 0;
      reader.section.reset();
    }
    reader.requestUpdate();
  } else {
    LOG_INF("BENCH", "=== PHASE 1 COMPLETE: %d sections, %d total pages ===", totalSections, totalPages);
    logPhaseEnd("cold_parse");

    LOG_INF("BENCH", "=== PHASE 2: WARM RENDER (%d page turns) ===", BenchmarkState::pageTurnCount);
    {
      EpubReaderActivity::RenderLock lock(reader);
      reader.currentSpineIndex = 0;
      reader.nextPageNumber = 0;
      reader.section.reset();
    }
    phase = Phase::WARM_RENDER;
    phaseStartMs = millis();
    pagesRemaining = BenchmarkState::pageTurnCount;
    lastPageTurnMs = phaseStartMs;
    reader.requestUpdate();
  }
}

void EpubBenchmarkDriver::loopWarmRender() {
  if (pagesRemaining <= 0) {
    LOG_INF("BENCH", "=== PHASE 2 COMPLETE ===");
    logPhaseEnd("warm_render");

    // Prepare Phase 3: chapter jumps
    const int spineCount = reader.epub->getSpineItemsCount();
    const int jumpCount = BenchmarkState::CHAPTER_JUMP_COUNT;
    LOG_INF("BENCH", "=== PHASE 3: CHAPTER JUMPS (%d jumps) ===", jumpCount);
    for (int i = 0; i < jumpCount; i++) {
      jumpTargets[i] = (spineCount * (i + 1)) / (jumpCount + 1);
      LOG_INF("BENCH", "  Jump target %d: spine %d", i, jumpTargets[i]);
    }
    jumpIndex = 0;
    jumpWaitingForRender = false;
    phase = Phase::CHAPTER_JUMPS;
    phaseStartMs = millis();
    {
      EpubReaderActivity::RenderLock lock(reader);
      reader.currentSpineIndex = jumpTargets[0];
      reader.nextPageNumber = 0;
      reader.section.reset();
    }
    reader.requestUpdate();
    return;
  }

  if (millis() - lastPageTurnMs < BenchmarkState::PAGE_TURN_DELAY_MS) {
    return;
  }

  {
    EpubReaderActivity::RenderLock lock(reader);
    if (!reader.section) {
      return;
    }

    lastPageTurnMs = millis();
    pagesRemaining--;

    if (reader.section->currentPage < reader.section->pageCount - 1) {
      reader.section->currentPage++;
    } else {
      if (reader.currentSpineIndex + 1 < reader.epub->getSpineItemsCount()) {
        reader.nextPageNumber = 0;
        reader.currentSpineIndex++;
        reader.section.reset();
      } else {
        LOG_INF("BENCH", "[PHASE2] Reached end of book with %d turns remaining", pagesRemaining);
        pagesRemaining = 0;
        return;
      }
    }
  }
  reader.requestUpdate();
}

void EpubBenchmarkDriver::loopChapterJumps() {
  if (!jumpWaitingForRender) {
    jumpWaitingForRender = true;
    return;
  }

  {
    EpubReaderActivity::RenderLock lock(reader);
    if (!reader.section) {
      return;
    }
  }

  LOG_INF("BENCH", "[PHASE3] Jump %d/%d: spine %d rendered", jumpIndex + 1, BenchmarkState::CHAPTER_JUMP_COUNT,
          jumpTargets[jumpIndex]);

  jumpIndex++;
  if (jumpIndex < BenchmarkState::CHAPTER_JUMP_COUNT) {
    jumpWaitingForRender = false;
    {
      EpubReaderActivity::RenderLock lock(reader);
      reader.currentSpineIndex = jumpTargets[jumpIndex];
      reader.nextPageNumber = 0;
      reader.section.reset();
    }
    reader.requestUpdate();
  } else {
    LOG_INF("BENCH", "=== PHASE 3 COMPLETE ===");
    logPhaseEnd("chapter_jumps");

    // Trigger warm re-open: exit reader, main.cpp will immediately re-open.
    // Set phase to DONE so this doesn't re-execute while pendingGoHome is processed.
    phase = Phase::DONE;
    BenchmarkState::pendingReopen = true;
    reader.pendingGoHome = true;
  }
}

void EpubBenchmarkDriver::loopWarmReopenWait() {
  {
    EpubReaderActivity::RenderLock lock(reader);
    if (!reader.section) {
      return;
    }
  }

  LOG_INF("BENCH", "=== PHASE 4 COMPLETE: first page rendered ===");
  logPhaseEnd("warm_reopen");
  phase = Phase::DONE;
}

void EpubBenchmarkDriver::finish() {
  if (BenchmarkState::pendingReopen) {
    return;
  }

  unsigned long totalMs = millis() - BenchmarkState::benchmarkStartMs;

  BenchmarkAllocTracker::logHeapSnapshot("final");
  LOG_INF("BENCH", "[WALL_TIME] total: %lu ms", totalMs);
  logSerial.flush();
  LOG_INF("BENCH", "=== BENCHMARK COMPLETE: total_time=%lu ms sections=%d pages=%d ===", totalMs, totalSections,
          totalPages);

  BenchmarkState::requested = false;
  reader.pendingGoHome = true;
}

#endif  // BENCHMARK_MODE
