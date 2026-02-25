# Benchmark Test Plan

## Prerequisites

- Device connected via USB
- Test EPUB copied to the device's SD card (e.g. `/books/jane-austen_pride-and-prejudice.epub`)
- `pyserial` installed: `pip3 install pyserial`

## Phase 1: Capture the baseline

**1. Check out the benchmark harness branch and build:**

```bash
git checkout benchmark-harness
pio run -e benchmark
```

**2. Flash the device:**

```bash
pio run -e benchmark -t upload
```

**3. Wait for the device to boot to the home screen**, then run the capture script:

```bash
python scripts/bench_capture.py \
  --label baseline \
  --book /books/jane-austen_pride-and-prejudice.epub \
  --pages 50 \
  --outdir logs
```

This sends `CMD:BENCHMARK` over serial, streams all output to the terminal, and
writes it to `logs/bench_baseline_<timestamp>.log`. It exits automatically when
it sees `BENCHMARK COMPLETE`.

**4. Verify the log looks sane** — you should see section parse times, page
render times, allocation counters, and heap snapshots scrolling by.

## Phase 2: Capture the change

**5. Create a feature branch from the harness:**

```bash
git checkout -b bench-<feature> benchmark-harness
```

**6. Cherry-pick the performance-sensitive commit(s):**

```bash
git cherry-pick <commit-hash>
```

If the change spans multiple commits, cherry-pick them in order. Resolve any
conflicts if needed.

**7. Build and flash the changed firmware:**

```bash
pio run -e benchmark
pio run -e benchmark -t upload
```

**8. Wait for the device to boot to home**, then capture again with a different
label:

```bash
python scripts/bench_capture.py \
  --label <feature> \
  --book /books/jane-austen_pride-and-prejudice.epub \
  --pages 50 \
  --outdir logs
```

## Phase 3: Compare

**9. Run the analysis script with both log files:**

```bash
python scripts/bench_analyze.py \
  logs/bench_baseline_*.log \
  logs/bench_<feature>_*.log
```

This prints individual summaries for each run, then a comparison table with
deltas and percentages. It also emits a markdown table you can copy directly
into a PR description.

## Phase 4: Clean up

**10. Switch back to your working branch:**

```bash
git checkout <your-branch>
```

The temporary bench branch can be deleted once you've captured the data:

```bash
git branch -D bench-<feature>
```

## Notes

- The baseline log is reusable as long as the harness branch hasn't changed.
  Just point `bench_analyze.py` at the same baseline file and a new capture.
- To re-run a capture for extra confidence, run `bench_capture.py` again with
  the same label. The timestamp in the filename keeps them from colliding.
- The `--pages` flag controls how many page turns happen in Phase 2 of the
  benchmark (warm render). Default is 50.
- The `--timeout` flag (default 600s) controls how long the capture script
  waits before giving up.
