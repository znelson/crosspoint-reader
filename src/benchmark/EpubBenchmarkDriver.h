#pragma once
#ifdef BENCHMARK_MODE

#include "benchmark/BenchmarkAllocTracker.h"
#include "benchmark/BenchmarkState.h"

#include <HalStorage.h>

class EpubReaderActivity;

class EpubBenchmarkDriver {
 public:
  explicit EpubBenchmarkDriver(EpubReaderActivity& reader);

  void start();
  void startWarmReopen();
  void loop();
  bool isWarmRender() const;

 private:
  enum class Phase { COLD_PARSE, WARM_RENDER, CHAPTER_JUMPS, WARM_REOPEN_WAIT, DONE };

  void loopColdParse();
  void loopWarmRender();
  void loopChapterJumps();
  void loopWarmReopenWait();
  void finish();
  void logSdIoCounters(const char* label);
  void logPhaseEnd(const char* label);

  EpubReaderActivity& reader;
  Phase phase = Phase::COLD_PARSE;
  int pagesRemaining = 0;
  int totalSections = 0;
  int totalPages = 0;
  unsigned long lastPageTurnMs = 0;
  unsigned long startMs = 0;
  unsigned long phaseStartMs = 0;

  int jumpIndex = 0;
  int jumpTargets[BenchmarkState::CHAPTER_JUMP_COUNT] = {};
  bool jumpWaitingForRender = false;
};

#endif  // BENCHMARK_MODE
