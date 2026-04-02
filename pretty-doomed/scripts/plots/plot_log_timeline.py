#!/usr/bin/env python3
"""Parse pretty-doomed log files and generate phase timeline plots.

Derives phase boundaries from log messages and plots a Gantt-style timeline
showing what each thread/CPU is doing over time. Works with the [cN/tM] tags
in log lines.

Usage:
    python3 scripts/plots/plot_log_timeline.py <log_file> <output_png> <title>
    python3 scripts/plots/plot_log_timeline.py --batch  # process all runs in toGround/
    python3 scripts/plots/plot_log_timeline.py --batch --input-dir docs/data --output-dir docs/data
"""

import argparse
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
    ("SDR Init",       re.compile(r"AD9361(?! hardware FIR disabled)|Building flowgraph")),
    ("SDR Capture",        re.compile(r"Starting capture|Capture complete|Timeout:|Progress \[")),
    ("SDR Teardown",       re.compile(r"Stopping flowgraph")),
    ("Normalize",      re.compile(r"Normalizing audio|RMS normalize:")),
    ("Artifacts",      re.compile(r"IQ diag:|IQ RMS:|IQ zeros:|IQ metrics:|Spectrogram:|Constellation:|PSD:")),
    ("DSP Filter",     re.compile(r"Filtering \(GNU Radio\)|Resampling to")),
    ("STT Inference",  re.compile(r"Transcribing \(|STT time:")),
    ("Detection",      re.compile(r"Detecting command|Wake word:|Command \[|Force-triggering|Command detected")),
    ("DOOM",           re.compile(r"Running DOOM demo:|Completed demo:")),
    ("Postcard",       re.compile(r"Generating postcard|Postcard:|Frame:|SC16:")),
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
    "Detection":      "#E67E22",
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


def annotate_capture_numbers(entries):
    """Add capture number suffix to phase names for multi-capture runs.

    Detects '=== Capture N/M ===' markers and '--- Processing: .../capture-NNN/...'
    markers.  When M > 1 every phase is labelled ' #N' so the Gantt chart
    distinguishes capture iterations.  STT Model Load is never numbered
    (it is not capture-specific).

    In sequential mode, processing phases appear after all captures are done.
    The '--- Processing:' marker resets the capture number so that processing
    of capture 1 is correctly labelled #1 even though it occurs after the
    '=== Capture 2/2 ===' marker.
    """
    capture_re = re.compile(r"=== Capture (\d+)/(\d+) ===")
    processing_re = re.compile(r"--- Processing: .*/capture-(\d+)/")

    # Collect capture markers
    markers = []  # (time, cap_num, cap_total, tid)
    for t, cpu, tid, phase, msg in entries:
        m = capture_re.search(msg)
        if m:
            markers.append((t, int(m.group(1)), int(m.group(2)), tid))

    if not markers or markers[0][2] <= 1:
        return entries  # single capture — nothing to annotate

    main_tid = markers[0][3]
    cap_boundaries = [(m[0], m[1]) for m in markers]

    def capture_at(t):
        cap = 0
        for bt, bn in cap_boundaries:
            if t >= bt:
                cap = bn
            else:
                break
        return cap

    # For each non-main thread, assign capture number from file paths in its
    # log messages (e.g. capture-001/spectrogram.bmp -> capture 1). This is
    # more reliable than capture_at(t) which can misattribute when a thread
    # starts just after the next capture marker is logged. We scan all
    # messages from the thread since the first line may not contain a path
    # (e.g. "IQ diag: N samples analyzed" has no path).
    capture_path_re = re.compile(r"capture-0*(\d+)/")
    thread_capture = {}
    for t, cpu, tid, phase, msg in entries:
        if tid != main_tid and tid not in thread_capture:
            mp = capture_path_re.search(msg)
            if mp:
                thread_capture[tid] = int(mp.group(1))
    # Second pass: assign remaining threads by timestamp
    for t, cpu, tid, phase, msg in entries:
        if tid != main_tid and tid not in thread_capture:
            thread_capture[tid] = capture_at(t)

    # Phases that should never be numbered
    unnumbered_phases = {"STT Model Load"}

    result = []
    main_cap = 0
    for t, cpu, tid, phase, msg in entries:
        m = capture_re.search(msg)
        if m and tid == main_tid:
            main_cap = int(m.group(1))

        # Sequential mode: '--- Processing: .../capture-NNN/...' resets capture number
        mp = processing_re.search(msg)
        if mp and tid == main_tid:
            main_cap = int(mp.group(1))

        if phase is not None and phase not in unnumbered_phases:
            cap = main_cap if tid == main_tid else thread_capture.get(tid, 0)
            if cap > 0:
                phase = f"{phase} #{cap}"

        result.append((t, cpu, tid, phase, msg))
    return result


def phase_base(name):
    """Strip ' #N' suffix to get the base phase name."""
    idx = name.rfind(" #")
    return name[:idx] if idx != -1 else name


def relocate_capture_labels(spans, tid_labels):
    """Move capture number from bar labels to thread y-axis labels.

    For threads that only process a single capture, strip ' #N' from
    the phase names and append '[Cap #N]' to the thread label instead.
    Threads handling multiple captures (e.g. the main capture thread)
    keep '#N' on each bar.
    """
    thread_caps = defaultdict(set)
    for phase, tid, ts, te in spans:
        if phase_base(phase) != phase:
            cap = phase[phase.rfind(" #") + 2:]
            thread_caps[tid].add(cap)

    single = {tid: caps.pop() for tid, caps in thread_caps.items()
              if len(caps) == 1}

    new_labels = dict(tid_labels)
    for tid, cap in single.items():
        if tid in new_labels:
            new_labels[tid] += f"  [Cap #{cap}]"

    new_spans = []
    for phase, tid, ts, te in spans:
        if tid in single:
            phase = phase_base(phase)
        new_spans.append((phase, tid, ts, te))

    return new_spans, new_labels


def build_phase_spans(entries):
    """Convert entries into (phase, tid, t_start, t_end) spans."""
    # Group consecutive same-phase entries per thread
    spans = []
    active = {}  # tid -> (phase, t_start, t_last)

    for t, cpu, tid, phase, msg in entries:
        if phase is None:
            # Close active phase on this thread (unrecognized log line ends the span)
            if tid in active:
                prev_phase, prev_start, prev_end = active[tid]
                spans.append((prev_phase, tid, prev_start, prev_end))
                del active[tid]
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

    # Move capture #N from bar labels to thread labels where possible
    spans, tid_labels = relocate_capture_labels(spans, tid_labels)

    tid_y = {tid: i for i, tid in enumerate(tids)}
    t_max = max(e[0] for e in entries)

    fig, ax = plt.subplots(figsize=(14, max(2.5, len(tids) * 1.2 + 1)))

    bar_height = 0.6
    for phase, tid, ts, te in spans:
        color = PHASE_COLORS.get(phase_base(phase), "#CCCCCC")
        duration = max(te - ts, 0.02)  # minimum visible width
        y = tid_y[tid]
        ax.barh(y, duration, left=ts, height=bar_height, color=color,
                edgecolor="white", linewidth=0.5, alpha=0.85)

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

    # Phase name + duration labels on bars
    LABEL_PHASES = {"STT Model Load", "SDR Init", "SDR Capture", "STT Inference", "DOOM", "Postcard"}
    x_range = (x_max if x_max else t_max)
    for phase, tid, ts, te in spans:
        if phase_base(phase) not in LABEL_PHASES or (te - ts) < 0.5:
            continue
        secs = te - ts
        bar_frac = secs / x_range if x_range > 0 else 0
        mid = (ts + te) / 2
        y = tid_y[tid]
        if bar_frac > 0.04:
            label = f"{phase}\n{secs:.1f}s"
        else:
            label = f"{secs:.1f}s"
        ax.text(mid, y, label, ha="center", va="center",
                fontsize=7, fontweight="bold", color="white", alpha=0.9)

    # Legend outside plot area
    base_order = ["STT Model Load", "SDR Init", "SDR Capture", "SDR Teardown",
                  "Normalize", "Artifacts", "DSP Filter", "STT Inference",
                  "Detection", "DOOM", "Postcard"]
    seen_bases = set(phase_base(s[0]) for s in spans)
    handles = []
    for phase in base_order:
        if phase in seen_bases:
            handles.append(mpatches.Patch(color=PHASE_COLORS[phase], label=phase))
    if handles:
        ax.legend(handles=handles, loc="upper left", bbox_to_anchor=(1.01, 1),
                  fontsize=8, framealpha=0.9)

    fig.tight_layout()
    fig.savefig(output_path, dpi=150, bbox_inches="tight")
    plt.close(fig)
    print(f"  Saved: {output_path}")


def collect_log_files(path):
    """Return list of log files for a path (file or run directory)."""
    if os.path.isdir(path):
        logs = []
        main_log = os.path.join(path, "pretty-doomed.log")
        if os.path.isfile(main_log):
            logs.append(main_log)
        for cap in sorted(os.listdir(path)):
            cap_log = os.path.join(path, cap, "run.log")
            if os.path.isfile(cap_log):
                logs.append(cap_log)
        return logs
    return [path]


def process_log(log_path, output_path, title, x_max=None):
    log_files = collect_log_files(log_path)
    entries = parse_log(log_files)
    entries = annotate_capture_numbers(entries)
    spans = build_phase_spans(entries)
    plot_timeline(entries, spans, output_path, title, x_max=x_max)


RUN_TITLES = {}


def main():
    parser = argparse.ArgumentParser(description="Phase timeline plot generator")
    parser.add_argument("log_file", nargs="?", help="Log file or run directory")
    parser.add_argument("output_png", nargs="?", help="Output PNG path")
    parser.add_argument("title", nargs="?", help="Plot title")
    parser.add_argument("--batch", action="store_true",
                        help="Process all runs in input directory")
    parser.add_argument("--input-dir",
                        help="Directory containing run-* dirs (default: toGround/)")
    parser.add_argument("--output-dir",
                        help="Output directory for plots")
    args = parser.parse_args()

    if args.batch:
        base = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
        toground = args.input_dir or os.path.join(base, "toGround")

        if args.output_dir:
            outdir = args.output_dir
        else:
            local_dir = os.path.join(base, "artifacts", "plots", "local")
            os.makedirs(local_dir, exist_ok=True)
            idx = 1
            for d in os.listdir(local_dir):
                if d.isdigit():
                    idx = max(idx, int(d) + 1)
            outdir = os.path.join(local_dir, f"{idx:03d}")
        os.makedirs(outdir, exist_ok=True)

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
            entries = annotate_capture_numbers(entries)
            spans = build_phase_spans(entries)
            title = RUN_TITLES.get(run_dir, run_dir)
            out = os.path.join(outdir, f"{run_dir}-timeline.png")
            jobs.append((entries, spans, out, title))

        global_x_max = max((e[0] for entries, _, _, _ in jobs for e in entries), default=0)

        # Second pass: plot with shared x-axis scale
        for entries, spans, out, title in jobs:
            plot_timeline(entries, spans, out, title, x_max=global_x_max)

        return

    if args.log_file and args.output_png and args.title:
        process_log(args.log_file, args.output_png, args.title)
        return

    parser.print_help()
    sys.exit(1)


if __name__ == "__main__":
    main()
