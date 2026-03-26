#!/usr/bin/env python3
"""Parse pretty-doomed log files and generate phase timeline plots.

Derives phase boundaries from log messages and plots a Gantt-style timeline
showing what each thread/CPU is doing over time. Works with the [cN/tM] tags
in log lines.

Usage:
    python3 scripts/plot_log_timeline.py <log_file> <output_png> <title>
    python3 scripts/plot_log_timeline.py --batch  # process all runs in toGround/
"""

import re
import sys
import os
from datetime import datetime
from collections import defaultdict

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
from matplotlib.patches import FancyBboxPatch

# Phase detection patterns (order matters: first match wins)
PHASE_PATTERNS = [
    ("STT Model Load", re.compile(r"Loading STT model")),
    ("STT Model Load", re.compile(r"STT model loaded")),
    ("STT Model Load", re.compile(r"Encoder:|Decoder:|Joiner:|Tokens:")),
    ("SDR Init",       re.compile(r"=== Capture \d+/\d+ ===")),
    ("SDR Init",       re.compile(r"Configuring SDR|SDR Capture:")),
    ("SDR Init",       re.compile(r"AD9361|Building flowgraph")),
    ("SDR Capture",        re.compile(r"Starting capture|Capture complete|Timeout:|Progress \[")),
    ("SDR Teardown",       re.compile(r"Stopping flowgraph")),
    ("Normalize",      re.compile(r"Normalizing audio|RMS normalize:")),
    ("Artifacts",      re.compile(r"IQ diag:|IQ RMS:|IQ zeros:|Spectrogram:|Constellation:")),
    ("DSP Filter",     re.compile(r"Filtering \(GNU Radio\)|Resampling to")),
    ("STT Inference",  re.compile(r"Transcribing \(|STT time:")),
    ("Detection",      re.compile(r"Detecting command|Wake word:|Command \[|Force-triggering|Command detected")),
    ("DOOM",           re.compile(r"Running DOOM demo:|Completed demo:")),
    ("Postcard",       re.compile(r"Generating postcard|Postcard:")),
]

PHASE_COLORS = {
    "STT Model Load": "#7B68EE",
    "SDR Init":       "#5DADE2",
    "SDR Capture":        "#2E86C1",
    "SDR Teardown":       "#85929E",
    "Normalize":      "#F39C12",
    "Artifacts":      "#1ABC9C",
    "DSP Filter":     "#FFA500",
    "STT Inference":  "#FF6B6B",
    "Detection":      "#2ECC71",
    "DOOM":           "#9B59B6",
    "Postcard":       "#87CEEB",
}

LOG_RE = re.compile(
    r"\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})\]"
    r"\[c(\d+)/t(\d+)\]\s*(.*)"
)


def parse_log(path):
    """Parse log file into list of (timestamp_sec, cpu, tid, phase, msg).

    Accepts a single file path or a list of paths. When multiple files are
    given their entries are merged and sorted by timestamp.
    """
    paths = path if isinstance(path, list) else [path]
    raw = []
    for p in paths:
        for line in open(p):
            m = LOG_RE.match(line)
            if not m:
                continue
            ts_str = m.group(1)
            ts = datetime.strptime(ts_str, "%Y-%m-%d %H:%M:%S.%f")
            raw.append((ts, int(m.group(2)), int(m.group(3)), m.group(4)))

    raw.sort(key=lambda x: x[0])

    entries = []
    t0 = raw[0][0] if raw else None
    for ts, cpu, tid, msg in raw:
        t = (ts - t0).total_seconds()
        phase = None
        for pname, pat in PHASE_PATTERNS:
            if pat.search(msg):
                phase = pname
                break
        entries.append((t, cpu, tid, phase, msg))
    return entries


def build_phase_spans(entries):
    """Convert entries into (phase, tid, t_start, t_end) spans."""
    # Group consecutive same-phase entries per thread
    spans = []
    active = {}  # tid -> (phase, t_start, t_last)

    for t, cpu, tid, phase, msg in entries:
        if phase is None:
            continue

        if tid in active:
            prev_phase, prev_start, prev_end = active[tid]
            if phase == prev_phase:
                active[tid] = (phase, prev_start, t)
                continue
            else:
                spans.append((prev_phase, tid, prev_start, t))

        active[tid] = (phase, t, t)

    # Flush remaining
    for tid, (phase, ts, te) in active.items():
        spans.append((phase, tid, ts, te))

    return spans


def plot_timeline(entries, spans, output_path, title, x_max=None):
    """Generate a Gantt-style timeline plot. If x_max is given, use it for the x-axis limit."""
    if not entries:
        print(f"  No entries to plot for {title}")
        return

    # Identify threads
    tids = sorted(set(e[2] for e in entries))
    tid_labels = {}
    for tid in tids:
        # Find the most common cpu for this tid
        cpus = [e[1] for e in entries if e[2] == tid]
        main_cpu = max(set(cpus), key=cpus.count)
        tid_labels[tid] = f"CPU{main_cpu} / Thread {tid}"

    tid_y = {tid: i for i, tid in enumerate(tids)}
    t_max = max(e[0] for e in entries)

    fig, ax = plt.subplots(figsize=(14, max(2.5, len(tids) * 1.2 + 1)))

    bar_height = 0.6
    for phase, tid, ts, te in spans:
        color = PHASE_COLORS.get(phase, "#CCCCCC")
        duration = max(te - ts, 0.02)  # minimum visible width
        y = tid_y[tid]
        ax.barh(y, duration, left=ts, height=bar_height, color=color,
                edgecolor="white", linewidth=0.5, alpha=0.85)

    # Dashed vertical lines at capture boundaries (skip the first)
    capture_re = re.compile(r"=== Capture (\d+)/(\d+) ===")
    capture_starts = []
    for t, cpu, tid, phase, msg in entries:
        m = capture_re.search(msg)
        if m:
            capture_starts.append((t, int(m.group(1)), tid))
    if len(capture_starts) > 1:
        for t, cap_num, tid in capture_starts[1:]:
            y = tid_y.get(tid, 0)
            ax.axvline(x=t, color="#333333", linestyle="--", linewidth=1, alpha=0.6)

    # CPU migration markers: show when a thread switches CPU
    prev_cpu = {}
    for t, cpu, tid, phase, msg in entries:
        if tid in prev_cpu and prev_cpu[tid] != cpu:
            y = tid_y[tid]
            ax.plot(t, y, "k|", markersize=10, markeredgewidth=1.5, alpha=0.4)
        prev_cpu[tid] = cpu

    ax.set_yticks(range(len(tids)))
    ax.set_yticklabels([tid_labels[tid] for tid in tids])
    ax.set_xlabel("Time (seconds)")
    ax.set_title(title, fontsize=13, fontweight="bold")
    ax.set_xlim(-0.05, (x_max if x_max else t_max) + 0.1)
    ax.invert_yaxis()
    ax.grid(axis="x", alpha=0.3)

    # Legend outside plot area, with total duration per phase
    phase_durations = defaultdict(float)
    for phase, tid, ts, te in spans:
        phase_durations[phase] += te - ts

    handles = []
    for phase in ["STT Model Load", "SDR Init", "SDR Capture", "SDR Teardown",
                   "Normalize", "Artifacts", "DSP Filter", "STT Inference",
                   "Detection", "DOOM", "Postcard"]:
        if any(s[0] == phase for s in spans):
            secs = phase_durations[phase]
            label = f"{phase} ({secs:.3f}s)"
            handles.append(mpatches.Patch(color=PHASE_COLORS[phase], label=label))
    if handles:
        ax.legend(handles=handles, loc="upper left", bbox_to_anchor=(1.01, 1),
                  fontsize=8, framealpha=0.9)

    fig.tight_layout()
    fig.savefig(output_path, dpi=150, bbox_inches="tight")
    plt.close(fig)
    print(f"  Saved: {output_path}")


def process_log(log_path, output_path, title, x_max=None):
    entries = parse_log(log_path)
    spans = build_phase_spans(entries)
    plot_timeline(entries, spans, output_path, title, x_max=x_max)


def main():
    if len(sys.argv) == 4:
        process_log(sys.argv[1], sys.argv[2], sys.argv[3])
        return

    if len(sys.argv) == 2 and sys.argv[1] == "--batch":
        base = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        toground = os.path.join(base, "toGround")
        local_dir = os.path.join(base, "artifacts", "plots", "local")
        os.makedirs(local_dir, exist_ok=True)

        # Auto-increment: find next index under local/
        idx = 1
        for d in os.listdir(local_dir):
            if d.isdigit():
                idx = max(idx, int(d) + 1)
        outdir = os.path.join(local_dir, f"{idx:03d}")
        os.makedirs(outdir)

        run_titles = {
            "run-00001": "Run 1: SDR Capture (sequential, stt_concurrent_load=false)",
            "run-00002": "Run 2: SDR Capture (background, stt_concurrent_load=false)",
            "run-00003": "Run 3: SDR Capture (background, stt_concurrent_load=true)",
        }

        # First pass: parse all runs and find global x_max
        jobs = []
        for run_dir in sorted(os.listdir(toground)):
            log_path = os.path.join(toground, run_dir, "pretty-doomed.log")
            if not os.path.isfile(log_path):
                continue
            log_files = [log_path]
            cap_dir = os.path.join(toground, run_dir)
            for cap in sorted(os.listdir(cap_dir)):
                cap_log = os.path.join(cap_dir, cap, "run.log")
                if os.path.isfile(cap_log):
                    log_files.append(cap_log)
            entries = parse_log(log_files)
            spans = build_phase_spans(entries)
            title = run_titles.get(run_dir, run_dir)
            out = os.path.join(outdir, f"{run_dir}-timeline.png")
            jobs.append((entries, spans, out, title))

        global_x_max = max((e[0] for entries, _, _, _ in jobs for e in entries), default=0)

        # Second pass: plot with shared x-axis scale
        for entries, spans, out, title in jobs:
            plot_timeline(entries, spans, out, title, x_max=global_x_max)

        return

    print(f"Usage: {sys.argv[0]} <log> <output.png> <title>")
    print(f"       {sys.argv[0]} --batch")
    sys.exit(1)


if __name__ == "__main__":
    main()
