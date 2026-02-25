### 1. Serial command received

The host script sends `CMD:BENCHMARK /books/jane-austen_pride-and-prejudice.epub 50` over serial. The main loop parses the book path and page count, sets `BenchmarkState::requested = true`, and calls `onGoToReader()` to navigate to the EPUB reader activity with that book.

### 2. Benchmark setup

When `EpubReaderActivity::onEnter()` fires, it sees the benchmark flag, creates an `EpubBenchmarkDriver`, and calls `start()`:

- Logs `BENCHMARK START` with the book path and spine item count.
- Takes an initial heap snapshot and resets SD I/O counters.
- **Clears the book cache entirely** (`section.reset()`, `epub->clearCache()`), ensuring cold-start conditions.
- Re-indexes the book (`epub->load()`), logging heap and SD I/O metrics for the indexing phase.
- Sets Phase 1 and rewinds to spine index 0.

### 3. Phase 1: Cold parse — every section, from scratch

The driver's `loopColdParse()` runs each main loop iteration:

- It acquires the render lock and waits for the render task to finish loading and parsing the current section (HTML parsing, CSS resolution, text layout, hyphenation, page building).
- Once the section is ready, it reads `pageCount` under the lock, then logs the spine index and page count for that section.
- It advances `currentSpineIndex` by one, resets the section, and triggers another render.
- This repeats for **every spine item in the book** — each one is parsed from the raw EPUB with no cache.

When the last spine item is done, it logs `PHASE 1 COMPLETE` with totals, takes a heap snapshot, logs SD I/O and wall time, then transitions to Phase 2.

### 4. Phase 2: Warm render — sequential page turns

`loopWarmRender()` rewinds back to spine index 0 and turns pages sequentially:

- Turns pages one at a time, waiting 500ms between each turn (`PAGE_TURN_DELAY_MS`).
- Each page turn either increments `currentPage` within the current section, or — if the section is exhausted — advances to the next spine item and resets the section (which loads from the section cache built in Phase 1).
- This continues for the requested number of page turns (default 50), or until the end of the book is reached.

When done, it logs phase-end metrics and transitions to Phase 3.

### 5. Phase 3: Chapter jumps — non-sequential section access

`loopChapterJumps()` tests section cache lookups across the full book:

- Computes 5 evenly-spaced spine indices across the book (e.g., for 65 sections: roughly spine 10, 21, 32, 43, 54).
- Jumps to each target spine index, resets the section, and waits for the render task to load and render that section from cache.
- Logs each jump completion.

This exercises the section cache with non-sequential access patterns, which is representative of users navigating via the table of contents. When all jumps are done, it logs phase-end metrics and initiates Phase 4.

### 6. Phase 4: Warm re-open — cached book load

After Phase 3, the driver sets `BenchmarkState::pendingReopen = true` and triggers `pendingGoHome`. This exits the reader activity entirely. In `main.cpp`, `onGoHome()` detects the pending re-open flag and calls `onGoToReader()` immediately instead of navigating to the home screen.

The new `ReaderActivity` loads the epub, creates a new `EpubReaderActivity`, which sees `BenchmarkState::requested` and `pendingReopen`. The new driver's `startWarmReopen()`:

- Logs `PHASE 4: WARM RE-OPEN` and resets SD I/O counters.
- Triggers a render and waits for the first page to be loaded and displayed.
- Measures the total time from re-open to first rendered page.

This is the most common real-world scenario: a user returning to a book they've already read. The book.bin cache, section caches, and progress data are all intact from prior phases.

### 7. Finish — report and go home

Once Phase 4 is done:

- Takes a final heap snapshot.
- Logs `BENCHMARK COMPLETE` with the total wall-clock time across all phases (including the activity teardown/rebuild for the warm re-open), section count, and page count.
- Clears `BenchmarkState::requested` and navigates to the home screen.

The `BENCHMARK COMPLETE` line is what tells `bench_capture.py` to stop recording and exit.

### What gets logged along the way

During all phases, the normal instrumented code fires, so the log captures:

- **Per-section**: parse time (ms), CSS resolve time and call count, layout/hyphenation time and call count, heap deltas, section cache hit/miss
- **Per-page**: render time (ms) tagged as "cold" (Phase 1) or "warm" (Phases 2-4)
- **Per-indexing-phase**: OPF pass, TOC pass, buildBookBin, total indexing (ms)
- **Per-phase**: heap snapshot, SD I/O counters (read/write opens, exists, removes, read/write timing), wall time
- **Snapshots**: heap state at initial, post-indexing, post-cold-parse, post-warm-render, post-chapter-jumps, post-warm-reopen, and final — including peak heap usage (min free ever) and fragmentation
