#pragma once
#ifdef BENCHMARK_MODE

#include <Logging.h>
#include <esp_heap_caps.h>

namespace BenchmarkAllocTracker {

struct HeapSnapshot {
  size_t allocatedBlocks;
  size_t totalAllocatedBytes;
  size_t largestFreeBlock;
  size_t totalFreeBytes;
  size_t minimumFreeEver;
};

HeapSnapshot takeHeapSnapshot();

void logHeapSnapshot(const char* label);
void logHeapDelta(const char* label, const HeapSnapshot& before, const HeapSnapshot& after);

}  // namespace BenchmarkAllocTracker

#endif  // BENCHMARK_MODE
