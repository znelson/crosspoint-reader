#ifdef BENCHMARK_MODE

#include "BenchmarkState.h"

namespace BenchmarkState {

bool requested = false;
bool pendingReopen = false;
unsigned long benchmarkStartMs = 0;
std::string bookPath;
int pageTurnCount = DEFAULT_PAGE_TURNS;

}  // namespace BenchmarkState

#endif  // BENCHMARK_MODE
