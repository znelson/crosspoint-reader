#!/usr/bin/env python3
"""
Benchmark log capture for CrossPoint Reader.

Connects to the device over serial, sends CMD:BENCHMARK, captures all output
to a timestamped log file, and exits when the benchmark completes.

Usage:
    python bench_capture.py --label baseline --book /path/on/device.epub
    python bench_capture.py --label change --book /path/on/device.epub --pages 100
"""

from __future__ import annotations

import argparse
import glob
import platform
import signal
import sys
import time
from datetime import datetime
from pathlib import Path

try:
    import serial
except ImportError:
    print("Error: pyserial not installed. Run: pip3 install pyserial")
    sys.exit(1)

shutdown = False


def signal_handler(signum, frame):
    global shutdown
    print("\nShutdown requested...")
    shutdown = True


def get_auto_detected_port() -> list[str]:
    system = platform.system()
    if system in ("Darwin", "Linux"):
        pattern = "/dev/tty.usbmodem*" if system == "Darwin" else "/dev/ttyACM*"
        return sorted(glob.glob(pattern))
    elif system == "Windows":
        from serial.tools import list_ports

        found = list_ports.comports()
        return [
            p.device
            for p in found
            if any(pat in p.description for pat in ["CP210x", "CH340", "USB Serial"])
            or p.hwid.startswith("USB VID:PID=303A:1001")
        ]
    return []


def main():
    parser = argparse.ArgumentParser(description="CrossPoint Benchmark Capture")
    parser.add_argument("--port", default=None, help="Serial port (auto-detect if omitted)")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--label", required=True, help="Label for this run (e.g. 'baseline', 'string-view')")
    parser.add_argument("--book", required=True, help="Path to EPUB on device SD card")
    parser.add_argument("--pages", type=int, default=50, help="Number of page turns in Phase 2 (default: 50)")
    parser.add_argument("--outdir", default=".", help="Directory for output log file")
    parser.add_argument(
        "--timeout", type=int, default=600, help="Max seconds to wait for benchmark completion (default: 600)"
    )
    args = parser.parse_args()

    signal.signal(signal.SIGINT, signal_handler)

    port = args.port
    if port is None:
        ports = get_auto_detected_port()
        if len(ports) == 1:
            port = ports[0]
            print(f"Auto-detected port: {port}")
        elif len(ports) > 1:
            print(f"Multiple ports found: {ports}")
            print("Specify one with --port")
            sys.exit(1)
        else:
            print("No serial port found. Specify with --port")
            sys.exit(1)

    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    outpath = outdir / f"bench_{args.label}_{timestamp}.log"

    try:
        ser = serial.Serial(port, args.baud, timeout=0.5)
        ser.dtr = False
        ser.rts = False
    except serial.SerialException as e:
        print(f"Error opening port: {e}")
        sys.exit(1)

    print(f"Connected to {port}")
    print(f"Log file: {outpath}")

    # Drain any pending data
    time.sleep(0.5)
    ser.reset_input_buffer()

    # Send benchmark command
    cmd = f"CMD:BENCHMARK {args.book}"
    if args.pages != 50:
        cmd += f" {args.pages}"
    print(f"Sending: {cmd}")
    ser.write(f"{cmd}\n".encode())

    start_time = time.time()
    benchmark_started = False
    benchmark_complete = False
    drain_until = None

    with open(outpath, "w") as logfile:
        # Write metadata header
        logfile.write(f"# label: {args.label}\n")
        logfile.write(f"# book: {args.book}\n")
        logfile.write(f"# pages: {args.pages}\n")
        logfile.write(f"# timestamp: {timestamp}\n")
        logfile.write(f"# port: {port}\n")
        logfile.write("#\n")

        while not shutdown:
            elapsed = time.time() - start_time
            if elapsed > args.timeout:
                print(f"\nTimeout after {args.timeout}s")
                break

            if drain_until and time.time() > drain_until:
                break

            try:
                raw = ser.readline().decode("utf-8", errors="replace").strip()
            except (OSError, UnicodeDecodeError):
                print("Serial error, disconnected?")
                break

            if not raw:
                continue

            logfile.write(raw + "\n")
            logfile.flush()
            print(raw)

            if "BENCHMARK START" in raw:
                benchmark_started = True
            if "BENCHMARK COMPLETE" in raw:
                benchmark_complete = True
                break
            if benchmark_started and "[WALL_TIME] total:" in raw and not drain_until:
                benchmark_complete = True
                drain_until = time.time() + 2.0

    ser.close()

    if benchmark_complete:
        print(f"\nBenchmark complete. Log saved to: {outpath}")
    elif benchmark_started:
        print(f"\nBenchmark interrupted (started but did not complete). Partial log: {outpath}")
    else:
        print(f"\nBenchmark did not start. Check device and book path. Log: {outpath}")

    sys.exit(0 if benchmark_complete else 1)


if __name__ == "__main__":
    main()
