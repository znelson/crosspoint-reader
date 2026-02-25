#ifdef BENCHMARK_MODE

#include "BenchmarkAllocTracker.h"

namespace BenchmarkAllocTracker {

HeapSnapshot takeHeapSnapshot() {
  multi_heap_info_t info;
  heap_caps_get_info(&info, MALLOC_CAP_DEFAULT);
  size_t minFree = heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT);
  return {info.allocated_blocks, info.total_allocated_bytes, info.largest_free_block, info.total_free_bytes, minFree};
}

void logHeapSnapshot(const char* label) {
  auto s = takeHeapSnapshot();
  LOG_INF("BENCH", "[HEAP] %s: blocks=%lu allocated=%lu largest_free=%lu free=%lu min_free_ever=%lu", label,
          static_cast<unsigned long>(s.allocatedBlocks), static_cast<unsigned long>(s.totalAllocatedBytes),
          static_cast<unsigned long>(s.largestFreeBlock), static_cast<unsigned long>(s.totalFreeBytes),
          static_cast<unsigned long>(s.minimumFreeEver));
}

void logHeapDelta(const char* label, const HeapSnapshot& before, const HeapSnapshot& after) {
  LOG_INF("BENCH", "[HEAP_DELTA] %s: blocks=%ld allocated=%ld largest_free=%ld free=%ld", label,
          static_cast<long>(after.allocatedBlocks) - static_cast<long>(before.allocatedBlocks),
          static_cast<long>(after.totalAllocatedBytes) - static_cast<long>(before.totalAllocatedBytes),
          static_cast<long>(after.largestFreeBlock) - static_cast<long>(before.largestFreeBlock),
          static_cast<long>(after.totalFreeBytes) - static_cast<long>(before.totalFreeBytes));
}

}  // namespace BenchmarkAllocTracker

#endif  // BENCHMARK_MODE
