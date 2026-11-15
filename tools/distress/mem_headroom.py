#!/usr/bin/env python3
"""Free memory of the SEPP in past pretty-doomed runs (issue #142).

Reads the resource.csv files of the EM runs (docs/changelog/data/em-*) and the
flight runs (docs/flight/data/run-*). For each run it gives:

  total       MemTotal
  idle        MemAvailable at the start of the run, before pretty-doomed starts
  min         the lowest MemAvailable during the run
  run_peak    idle - min, the memory that the v8 pipeline used

and then the margin of each distress config: idle - peak (VmHWM). Units: MiB.

Usage (from tools/distress):
  python3 mem_headroom.py [--peak NAME=MiB ...]
The default peaks are the SEPP emulator peaks of results/emu1g-*.
"""

import argparse
import csv
import datetime
import glob
import json
import os
import statistics

HERE = os.path.dirname(os.path.abspath(__file__))
APP = os.path.join(HERE, "..", "..", "pretty-doomed")
PATTERNS = {
    "EM": "docs/changelog/data/em-*/**/resource.csv",
    "flight": "docs/flight/data/run-*/resource.csv",
}
DEFAULT_PEAKS = {
    "parakeet-0.6b-v2-int8 (10 s)": os.path.join(HERE, "results", "emu1g-parakeet-0.6b-v2-int8", "run.json"),
    "parakeet-0.6b-v2-int8-w5": os.path.join(HERE, "results", "emu1g-parakeet-0.6b-v2-int8-w5", "run.json"),
}


def run_memory(path):
    """Return (date, total, idle, min) in MiB for one resource.csv, or None."""
    with open(path, encoding="utf-8") as f:
        rows = [r for r in csv.DictReader(f) if (r.get("mem_available_kb") or "").strip().isdigit()]
    if not rows:
        return None
    avail = [int(r["mem_available_kb"]) / 1024 for r in rows]
    date = datetime.datetime.fromtimestamp(int(rows[0]["epoch"]), datetime.timezone.utc).date()
    return date, int(rows[0]["mem_total_kb"]) / 1024, avail[0], min(avail)


def runs():
    """Yield (machine, run, date, total, idle, min) for each past run with memory data."""
    for machine, pattern in PATTERNS.items():
        for path in sorted(glob.glob(os.path.join(APP, pattern), recursive=True)):
            m = run_memory(path)
            if m is not None:
                run = os.path.relpath(os.path.dirname(path), os.path.join(APP, "docs"))
                yield (machine, run) + m


def idle_free(machine):
    """Return the idle free memory (MiB) at the start of each past run of a machine (EM or flight)."""
    return [r[4] for r in runs() if r[0] == machine]


def load_peaks(args_peaks):
    """Return {name: MiB} from --peak NAME=MiB, or from the emulator results."""
    if args_peaks:
        return {k: float(v) for k, v in (p.split("=", 1) for p in args_peaks)}
    peaks = {}
    for name, path in DEFAULT_PEAKS.items():
        if os.path.exists(path):
            with open(path, encoding="utf-8") as f:
                peaks[name] = json.load(f)["peak_rss_mb"]
    return peaks


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--peak", action="append", help="NAME=MiB, a peak memory to compare (repeatable)")
    args = parser.parse_args()

    print("| Machine | Run | Date | Total | Idle | Min | Run peak |")
    print("|---|---|---|---|---|---|---|")
    idle = {}
    for machine, run, date, total, start, low in runs():
        print(f"| {machine} | {run} | {date} | {total:.0f} | {start:.0f} | {low:.0f} | {start - low:.0f} |")
        idle.setdefault(machine, []).append(start)

    print("\n| Machine | Runs | Idle, lowest | Idle, median | Idle, highest |")
    print("|---|---|---|---|---|")
    for machine, values in idle.items():
        print(f"| {machine} | {len(values)} | {min(values):.0f} | {statistics.median(values):.0f} | {max(values):.0f} |")

    peaks = load_peaks(args.peak)
    if peaks:
        print("\n| Config | Peak | Margin, lowest idle | Margin, median idle |")
        print("|---|---|---|---|")
        for name, peak in peaks.items():
            for machine, values in idle.items():
                print(f"| {name}, {machine} | {peak:.0f} | {min(values) - peak:.0f} | {statistics.median(values) - peak:.0f} |")


if __name__ == "__main__":
    main()
