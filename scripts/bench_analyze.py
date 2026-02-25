#!/usr/bin/env python3
"""
Benchmark log analysis for CrossPoint Reader.

Parses timing and heap data from benchmark log files and produces summary
statistics. In comparison mode, generates a markdown table suitable for
PR descriptions.

Usage:
    python bench_analyze.py run.log                    # Single-file summary
    python bench_analyze.py baseline.log change.log    # Before/after comparison
"""

from __future__ import annotations

import argparse
import math
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path


@dataclass
class MetricSeries:
    name: str
    unit: str
    values: list[float] = field(default_factory=list)

    def add(self, v: float):
        self.values.append(v)

    @property
    def count(self) -> int:
        return len(self.values)

    @property
    def mean(self) -> float:
        return sum(self.values) / len(self.values) if self.values else 0

    @property
    def median(self) -> float:
        if not self.values:
            return 0
        s = sorted(self.values)
        n = len(s)
        if n % 2 == 1:
            return s[n // 2]
        return (s[n // 2 - 1] + s[n // 2]) / 2

    @property
    def p95(self) -> float:
        if not self.values:
            return 0
        s = sorted(self.values)
        idx = int(math.ceil(0.95 * len(s))) - 1
        return s[max(0, idx)]

    @property
    def stdev(self) -> float:
        if len(self.values) < 2:
            return 0
        m = self.mean
        return math.sqrt(sum((v - m) ** 2 for v in self.values) / (len(self.values) - 1))

    @property
    def min_val(self) -> float:
        return min(self.values) if self.values else 0

    @property
    def max_val(self) -> float:
        return max(self.values) if self.values else 0


# Patterns to extract from log lines.
# Benchmark builds log in microseconds; normal builds in milliseconds.
# Patterns accept both "us" and "ms" and normalize to microseconds.
PATTERNS: dict[str, tuple[str, str]] = {
    "Section parse time": (r"\[EHP\]\s+Time to parse and build pages:\s*(\d+)\s*(us|ms)", "us"),
    "Cold render time": (r"\[ERS\]\s+Rendered cold page in\s*(\d+)\s*(us|ms)", "us"),
    "Warm render time": (r"\[ERS\]\s+Rendered warm page in\s*(\d+)\s*(us|ms)", "us"),
    "Page render time": (r"\[ERS\]\s+Rendered (?:cold |warm )?page in\s*(\d+)\s*(us|ms)", "us"),
    "OPF pass": (r"\[EBP\]\s+OPF pass completed in\s*(\d+)\s*(us|ms)", "us"),
    "TOC pass": (r"\[EBP\]\s+TOC pass completed in\s*(\d+)\s*(us|ms)", "us"),
    "buildBookBin": (r"\[EBP\]\s+buildBookBin completed in\s*(\d+)\s*(us|ms)", "us"),
    "Total indexing": (r"\[EBP\]\s+Total indexing completed in\s*(\d+)\s*(us|ms)", "us"),
    "Display refresh": (r"\[GFX\]\s+Time\s*=\s*(\d+)\s*ms from clearScreen", "ms"),
    "CSS resolve time": (r"\[PARSE_DETAIL\]\s+css_resolve=(\d+)\s*us", "us"),
    "CSS resolve calls": (r"\[PARSE_DETAIL\]\s+css_resolve=\d+\s*us\s+\((\d+)\s+calls\)", "calls"),
    "Layout time": (r"\[PARSE_DETAIL\]\s+.*layout=(\d+)\s*us", "us"),
    "Layout calls": (r"\[PARSE_DETAIL\]\s+.*layout=\d+\s*us\s+\((\d+)\s+calls\)", "calls"),
}

HEAP_SNAPSHOT_PATTERN = re.compile(
    r"\[HEAP\]\s+(\S+):\s+blocks=(\d+)\s+allocated=(\d+)\s+largest_free=(\d+)\s+free=(\d+)\s+min_free_ever=(\d+)"
)

CACHE_PATTERN = re.compile(r"\[CACHE\]\s+spine=\d+\s+result=(hit|miss)")

SD_IO_PATTERN = re.compile(
    r"\[SD_IO\]\s+(\S+):\s+read_opens=(\d+)\s+write_opens=(\d+)\s+exists=(\d+)\s+removes=(\d+)"
    r"\s+read_time=(\d+)\s+us\s+write_time=(\d+)\s+us"
)

WALL_TIME_PATTERN = re.compile(r"\[WALL_TIME\]\s+(\S+):\s+(\d+)\s+ms")

HEAP_DELTA_PATTERN = re.compile(
    r"\[HEAP_DELTA\]\s+(\S+):\s+blocks=(-?\d+)\s+allocated=(-?\d+)\s+largest_free=(-?\d+)\s+free=(-?\d+)"
)


def parse_log(filepath: Path) -> dict[str, MetricSeries]:
    metrics: dict[str, MetricSeries] = {}

    for name, (pattern, unit) in PATTERNS.items():
        metrics[name] = MetricSeries(name, unit)

    heap_snapshots: dict[str, dict[str, float]] = {}
    section_heap_allocated = MetricSeries("Section parse heap delta", "bytes")
    section_heap_blocks = MetricSeries("Section parse block delta", "blocks")
    cache_hits = 0
    cache_misses = 0
    sd_io_phases: dict[str, dict[str, float]] = {}
    wall_times: dict[str, float] = {}

    benchmark_started = False

    with open(filepath) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue

            if "BENCHMARK START" in line:
                benchmark_started = True

            if not benchmark_started:
                continue

            for name, (pattern, unit) in PATTERNS.items():
                m = re.search(pattern, line)
                if m:
                    value = float(m.group(1))
                    if unit == "us" and m.lastindex and m.lastindex >= 2 and m.group(2) == "ms":
                        value *= 1000.0
                    metrics[name].add(value)

            m = HEAP_SNAPSHOT_PATTERN.search(line)
            if m:
                label = m.group(1)
                heap_snapshots[label] = {
                    "blocks": float(m.group(2)),
                    "allocated": float(m.group(3)),
                    "largest_free": float(m.group(4)),
                    "free": float(m.group(5)),
                    "min_free_ever": float(m.group(6)),
                }

            m = HEAP_DELTA_PATTERN.search(line)
            if m:
                label = m.group(1)
                if label.startswith("section_") and label.endswith("_parse"):
                    section_heap_allocated.add(float(m.group(3)))
                    section_heap_blocks.add(float(m.group(2)))

            m = CACHE_PATTERN.search(line)
            if m:
                if m.group(1) == "hit":
                    cache_hits += 1
                else:
                    cache_misses += 1

            m = WALL_TIME_PATTERN.search(line)
            if m:
                wall_times[m.group(1)] = float(m.group(2))

            m = SD_IO_PATTERN.search(line)
            if m:
                label = m.group(1)
                sd_io_phases[label] = {
                    "read_opens": float(m.group(2)),
                    "write_opens": float(m.group(3)),
                    "exists": float(m.group(4)),
                    "removes": float(m.group(5)),
                    "read_time_us": float(m.group(6)),
                    "write_time_us": float(m.group(7)),
                }

    if section_heap_allocated.count > 0:
        metrics["Section parse heap delta"] = section_heap_allocated
    if section_heap_blocks.count > 0:
        metrics["Section parse block delta"] = section_heap_blocks

    wall_time_labels = [
        ("indexing", "Wall time: indexing"),
        ("cold_parse", "Wall time: cold parse"),
        ("warm_render", "Wall time: warm render"),
        ("chapter_jumps", "Wall time: chapter jumps"),
        ("warm_reopen", "Wall time: warm re-open"),
        ("total", "Wall time: total"),
    ]
    for key, label in wall_time_labels:
        if key in wall_times:
            s = MetricSeries(label, "ms")
            s.add(wall_times[key])
            metrics[label] = s

    def add_snapshot_delta(name: str, unit: str, a_label: str, b_label: str, field: str):
        if a_label in heap_snapshots and b_label in heap_snapshots:
            s = MetricSeries(name, unit)
            s.add(heap_snapshots[b_label][field] - heap_snapshots[a_label][field])
            metrics[name] = s

    add_snapshot_delta("Indexing heap growth", "bytes", "initial", "indexing", "allocated")
    add_snapshot_delta("Cold parse heap growth", "bytes", "indexing", "cold_parse", "allocated")
    add_snapshot_delta("Warm render heap growth", "bytes", "cold_parse", "warm_render", "allocated")
    add_snapshot_delta("Chapter jumps heap growth", "bytes", "warm_render", "chapter_jumps", "allocated")
    add_snapshot_delta("Warm re-open heap growth", "bytes", "chapter_jumps", "warm_reopen", "allocated")
    add_snapshot_delta("Total heap growth", "bytes", "initial", "final", "allocated")

    if "final" in heap_snapshots:
        s = MetricSeries("Final free heap", "bytes")
        s.add(heap_snapshots["final"]["free"])
        metrics["Final free heap"] = s

        s = MetricSeries("Peak heap usage", "bytes")
        min_free = heap_snapshots["final"]["min_free_ever"]
        initial_free = heap_snapshots.get("initial", {}).get("free", 0)
        initial_alloc = heap_snapshots.get("initial", {}).get("allocated", 0)
        total_heap = initial_free + initial_alloc
        s.add(total_heap - min_free)
        metrics["Peak heap usage"] = s

        s = MetricSeries("Min free heap ever", "bytes")
        s.add(min_free)
        metrics["Min free heap ever"] = s

    for label in ("cold_parse", "warm_render", "chapter_jumps", "warm_reopen", "final"):
        if label in heap_snapshots:
            snap = heap_snapshots[label]
            free = snap["free"]
            largest = snap["largest_free"]
            if free > 0:
                frag_pct = (1.0 - largest / free) * 100.0
                s = MetricSeries(f"Fragmentation ({label})", "%")
                s.add(frag_pct)
                metrics[f"Fragmentation ({label})"] = s

    # SD I/O metrics: per-phase and totals
    sd_field_labels = [
        ("read_opens", "SD read opens", "count"),
        ("write_opens", "SD write opens", "count"),
        ("exists", "SD exists calls", "count"),
        ("removes", "SD remove calls", "count"),
        ("read_time_us", "SD read time", "us"),
        ("write_time_us", "SD write time", "us"),
    ]
    for phase_label, phase_data in sd_io_phases.items():
        for field_key, metric_name, unit in sd_field_labels:
            val = phase_data.get(field_key, 0)
            if val > 0:
                key = f"{metric_name} ({phase_label})"
                s = MetricSeries(key, unit)
                s.add(val)
                metrics[key] = s

    if len(sd_io_phases) > 1:
        for field_key, metric_name, unit in sd_field_labels:
            total = sum(p.get(field_key, 0) for p in sd_io_phases.values())
            if total > 0:
                key = f"{metric_name} (total)"
                s = MetricSeries(key, unit)
                s.add(total)
                metrics[key] = s

    total_cache = cache_hits + cache_misses
    if total_cache > 0:
        s = MetricSeries("Section cache hits", "count")
        s.add(cache_hits)
        metrics["Section cache hits"] = s

        s = MetricSeries("Section cache misses", "count")
        s.add(cache_misses)
        metrics["Section cache misses"] = s

        s = MetricSeries("Cache hit rate", "%")
        s.add(cache_hits / total_cache * 100.0)
        metrics["Cache hit rate"] = s

    return {k: v for k, v in metrics.items() if v.count > 0}


def _display_unit(unit: str) -> str:
    return "ms" if unit == "us" else unit


def _display_val(value: float, unit: str) -> float:
    return value / 1000.0 if unit == "us" else value


def _fmt(value: float, unit: str) -> str:
    v = _display_val(value, unit)
    if unit == "us":
        return f"{v:.2f}"
    if unit in ("bytes", "blocks", "calls", "count"):
        return f"{v:.0f}"
    if unit == "%":
        return f"{v:.1f}"
    return f"{v:.1f}"


def print_summary(label: str, metrics: dict[str, MetricSeries]):
    print(f"\n{'=' * 70}")
    print(f"  {label}")
    print(f"{'=' * 70}")
    print(f"{'Metric':<35} {'Count':>6} {'Mean':>10} {'Median':>10} {'P95':>10} {'Stdev':>10}")
    print("-" * 81)
    for m in metrics.values():
        print(
            f"{m.name:<35} {m.count:>6} {_fmt(m.mean, m.unit):>10} {_fmt(m.median, m.unit):>10} "
            f"{_fmt(m.p95, m.unit):>10} {_fmt(m.stdev, m.unit):>10}"
        )


def print_comparison(base_metrics: dict[str, MetricSeries], change_metrics: dict[str, MetricSeries],
                     base_label: str, change_label: str):
    all_keys = list(dict.fromkeys(list(base_metrics.keys()) + list(change_metrics.keys())))

    print(f"\n{'=' * 90}")
    print(f"  Comparison: {base_label} vs {change_label}")
    print(f"{'=' * 90}")

    # Console table
    print(f"{'Metric':<35} {'Base (mean)':>12} {'Change (mean)':>14} {'Delta':>10} {'Delta %':>10}")
    print("-" * 81)
    for key in all_keys:
        base = base_metrics.get(key)
        change = change_metrics.get(key)
        if not base or not change:
            continue
        delta_raw = change.mean - base.mean
        pct = (delta_raw / base.mean * 100) if base.mean != 0 else 0
        sign = "+" if delta_raw > 0 else ""
        print(f"{key:<35} {_fmt(base.mean, base.unit):>12} {_fmt(change.mean, change.unit):>14} "
              f"{sign}{_fmt(delta_raw, base.unit):>9} {sign}{pct:>9.1f}%")

    # Markdown table
    print(f"\n### Markdown (copy for PR)\n")
    print(f"| Metric | {base_label} (mean) | {change_label} (mean) | Delta | Delta % |")
    print(f"|---|---|---|---|---|")
    for key in all_keys:
        base = base_metrics.get(key)
        change = change_metrics.get(key)
        if not base or not change:
            continue
        delta_raw = change.mean - base.mean
        pct = (delta_raw / base.mean * 100) if base.mean != 0 else 0
        sign = "+" if delta_raw > 0 else ""
        dunit = _display_unit(base.unit)
        print(f"| {key} ({dunit}) | {_fmt(base.mean, base.unit)} | {_fmt(change.mean, change.unit)} "
              f"| {sign}{_fmt(delta_raw, base.unit)} | {sign}{pct:.1f}% |")


def extract_label(filepath: Path) -> str:
    """Try to extract the label from the log file metadata header."""
    try:
        with open(filepath) as f:
            for line in f:
                if line.startswith("# label:"):
                    return line.split(":", 1)[1].strip()
                if not line.startswith("#"):
                    break
    except Exception:
        pass
    return filepath.stem


def main():
    parser = argparse.ArgumentParser(description="CrossPoint Benchmark Analysis")
    parser.add_argument("logs", nargs="+", type=Path, help="Log file(s) to analyze")
    args = parser.parse_args()

    if len(args.logs) == 1:
        filepath = args.logs[0]
        if not filepath.exists():
            print(f"File not found: {filepath}")
            sys.exit(1)
        label = extract_label(filepath)
        metrics = parse_log(filepath)
        if not metrics:
            print("No benchmark data found in log file.")
            sys.exit(1)
        print_summary(label, metrics)

    elif len(args.logs) == 2:
        base_path, change_path = args.logs
        for p in (base_path, change_path):
            if not p.exists():
                print(f"File not found: {p}")
                sys.exit(1)
        base_label = extract_label(base_path)
        change_label = extract_label(change_path)
        base_metrics = parse_log(base_path)
        change_metrics = parse_log(change_path)
        if not base_metrics:
            print(f"No benchmark data found in {base_path}")
            sys.exit(1)
        if not change_metrics:
            print(f"No benchmark data found in {change_path}")
            sys.exit(1)
        print_summary(base_label, base_metrics)
        print_summary(change_label, change_metrics)
        print_comparison(base_metrics, change_metrics, base_label, change_label)
    else:
        print("Provide 1 log file (summary) or 2 log files (comparison).")
        sys.exit(1)


if __name__ == "__main__":
    main()
