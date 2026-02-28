#!/usr/bin/env python3
"""
Build firmware at each commit in a range and report flash usage.

Usage:
    python3 scripts/firmware_size_history.py <start_commit> <end_commit> [--env ENV] [--csv FILE]

Arguments:
    start_commit  Older commit (exclusive) — the baseline before the range.
    end_commit    Newer commit (inclusive) — the last commit to measure.

Options:
    --env ENV     PlatformIO build environment (default: "default")
    --csv FILE    Write CSV output to FILE instead of stdout.

The script walks every commit from start_commit to end_commit (oldest→newest),
checks each one out in a detached HEAD, runs `pio run`, parses the Flash usage
line, and records the result.  On completion (or Ctrl-C) the original branch or
HEAD is restored.

Example:
    python3 scripts/firmware_size_history.py HEAD~5 HEAD
    python3 scripts/firmware_size_history.py abc1234 def5678 --env gh_release --csv sizes.csv
"""

import argparse
import csv
import io
import os
import re
import subprocess
import sys

FLASH_RE = re.compile(
    r"Flash:.*?(\d+)\s+bytes\s+from\s+(\d+)\s+bytes"
)


def run(cmd, capture=True, check=True):
    result = subprocess.run(
        cmd, capture_output=capture, text=True, check=check
    )
    return result


def git_current_ref():
    """Return the current branch name, or the detached commit hash."""
    r = run(["git", "symbolic-ref", "--short", "HEAD"], check=False)
    if r.returncode == 0:
        return r.stdout.strip()
    return run(["git", "rev-parse", "HEAD"]).stdout.strip()


def git_commit_list(start, end):
    """Return list of (hash, title) from start (exclusive) to end (inclusive), oldest first."""
    r = run([
        "git", "log", "--reverse", "--format=%H %s",
        f"{start}..{end}",
    ])
    commits = []
    for line in r.stdout.strip().splitlines():
        if not line:
            continue
        sha, title = line.split(" ", 1)
        commits.append((sha, title))
    return commits


def git_checkout(ref):
    run(["git", "checkout", "--detach", ref], check=True)


def build_firmware(env):
    """Run pio build and return the raw combined stdout+stderr."""
    result = subprocess.run(
        ["pio", "run", "-e", env],
        capture_output=True, text=True, check=False
    )
    return result.returncode, result.stdout + "\n" + result.stderr


def parse_flash_used(output):
    """Extract used-bytes integer from PlatformIO output, or None."""
    m = FLASH_RE.search(output)
    if m:
        return int(m.group(1))
    return None


def main():
    parser = argparse.ArgumentParser(
        description="Measure firmware flash size across a git commit range."
    )
    parser.add_argument("start_commit", help="Older commit (exclusive baseline)")
    parser.add_argument("end_commit", help="Newer commit (inclusive)")
    parser.add_argument("--env", default="default", help="PlatformIO environment (default: 'default')")
    parser.add_argument("--csv", dest="csv_file", default=None, help="Output CSV file (default: stdout)")
    args = parser.parse_args()

    original_ref = git_current_ref()
    print(f"[info] Will restore to '{original_ref}' when finished.", file=sys.stderr)

    stash_needed = False
    status = run(["git", "status", "--porcelain"]).stdout.strip()
    if status:
        print("[info] Stashing uncommitted changes...", file=sys.stderr)
        run(["git", "stash", "push", "-m", "firmware_size_history auto-stash"])
        stash_needed = True

    commits = git_commit_list(args.start_commit, args.end_commit)
    if not commits:
        print(f"[error] No commits found in range {args.start_commit}..{args.end_commit}", file=sys.stderr)
        sys.exit(1)

    # Also build the baseline commit so we can compute the first delta
    baseline_sha = run(["git", "rev-parse", args.start_commit]).stdout.strip()
    baseline_title = run(["git", "log", "-1", "--format=%s", baseline_sha]).stdout.strip()
    all_commits = [(baseline_sha, baseline_title)] + commits

    print(f"[info] Building {len(all_commits)} commits ({1} baseline + {len(commits)} in range)...", file=sys.stderr)

    results = []
    try:
        for i, (sha, title) in enumerate(all_commits):
            short = sha[:10]
            label = "baseline" if i == 0 else f"{i}/{len(commits)}"
            print(f"\n[{label}] {short} {title}", file=sys.stderr)

            git_checkout(sha)

            print(f"  Building (env: {args.env})...", file=sys.stderr)
            rc, output = build_firmware(args.env)

            if rc != 0:
                print(f"  BUILD FAILED (exit {rc}) — skipping", file=sys.stderr)
                results.append((sha, title, None))
                continue

            used = parse_flash_used(output)
            if used is None:
                print("  Could not parse flash size from output — skipping", file=sys.stderr)
                results.append((sha, title, None))
                continue

            print(f"  Flash used: {used:,} bytes", file=sys.stderr)
            results.append((sha, title, used))

    except KeyboardInterrupt:
        print("\n[info] Interrupted — writing partial results.", file=sys.stderr)
    finally:
        print(f"\n[info] Restoring '{original_ref}'...", file=sys.stderr)
        run(["git", "checkout", original_ref], check=False)
        if stash_needed:
            print("[info] Restoring stashed changes...", file=sys.stderr)
            run(["git", "stash", "pop"], check=False)

    # Build CSV rows with deltas
    rows = []
    prev_size = None
    for sha, title, used in results:
        if used is not None and prev_size is not None:
            delta = used - prev_size
        else:
            delta = ""
        rows.append({
            "commit": sha[:10],
            "title": title,
            "flash_bytes": used if used is not None else "FAILED",
            "delta": delta,
        })
        if used is not None:
            prev_size = used

    fieldnames = ["commit", "title", "flash_bytes", "delta"]

    if args.csv_file:
        with open(args.csv_file, "w", newline="") as f:
            writer = csv.DictWriter(f, fieldnames=fieldnames)
            writer.writeheader()
            writer.writerows(rows)
        print(f"\n[done] Wrote {args.csv_file}", file=sys.stderr)
    else:
        buf = io.StringIO()
        writer = csv.DictWriter(buf, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)
        print(f"\n{buf.getvalue()}", end="")


if __name__ == "__main__":
    main()
