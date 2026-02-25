#pragma once
#ifdef BENCHMARK_MODE

#include <string>

namespace BenchmarkState {

// Set by the CMD:BENCHMARK serial command handler in main.cpp.
// Consumed by EpubReaderActivity when it enters.
extern bool requested;
extern bool pendingReopen;
extern unsigned long benchmarkStartMs;
extern std::string bookPath;
extern int pageTurnCount;

constexpr int DEFAULT_PAGE_TURNS = 50;
constexpr unsigned long PAGE_TURN_DELAY_MS = 500;
constexpr int CHAPTER_JUMP_COUNT = 5;

}  // namespace BenchmarkState

#endif  // BENCHMARK_MODE
