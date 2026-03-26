#!/usr/bin/env python3
"""Generate combined timeline + resource utilization plots.

Stacks a Gantt-style phase timeline on top with per-core CPU usage and memory
below, all sharing the same x-axis. This directly correlates which threads are
running with actual CPU/memory impact.

Usage:
    python3 scripts/plots/plot_resource.py --batch              # all runs in toGround/
    python3 scripts/plots/plot_resource.py toGround/run-00001   # single run
"""
import argparse, csv, os, re, sys
from pathlib import Path
from datetime import datetime
from collections import defaultdict

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
import numpy as np

# ---------------------------------------------------------------------------
# Phase detection (shared with plot_log_timeline.py)
# ---------------------------------------------------------------------------
PHASE_PATTERNS = [
    ("STT Model Load", re.compile(r"Loading STT model")),
    ("STT Model Load", re.compile(r"STT model loaded")),
    ("STT Model Load", re.compile(r"Encoder:|Decoder:|Joiner:|Tokens:")),
    ("SDR Init",       re.compile(r"=== Capture \d+/\d+ ===")),
    ("SDR Init",       re.compile(r"Configuring SDR|SDR Capture:")),
    ("SDR Init",       re.compile(r"AD9361|Building flowgraph")),
    ("SDR Capture",    re.compile(r"Starting capture|Capture complete|Timeout:|Progress \[")),
    ("SDR Teardown",   re.compile(r"Stopping flowgraph")),
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
    "SDR Capture":    "#2E86C1",
    "SDR Teardown":   "#85929E",
    "Normalize":      "#F39C12",
    "Artifacts":      "#1ABC9C",
    "DSP Filter":     "#FFA500",
    "STT Inference":  "#FF6B6B",
    "Detection":      "#2ECC71",
    "DOOM":           "#9B59B6",
    "Postcard":       "#87CEEB",
}

PHASE_ORDER = ["STT Model Load", "SDR Init", "SDR Capture", "SDR Teardown",
               "Normalize", "Artifacts", "DSP Filter", "STT Inference",
               "Detection", "DOOM", "Postcard"]

LOG_RE = re.compile(
    r"\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})\]"
    r"\[c(\d+)/t(\d+)\]\s*(.*)"
)

# ---------------------------------------------------------------------------
# Log parsing
# ---------------------------------------------------------------------------
def parse_log(paths):
    if isinstance(paths, str):
        paths = [paths]
    raw = []
    for p in paths:
        for line in open(p, errors="replace"):
            line = line.strip("\x00\n\r")
            m = LOG_RE.match(line)
            if not m:
                continue
            ts = datetime.strptime(m.group(1), "%Y-%m-%d %H:%M:%S.%f")
            raw.append((ts, int(m.group(2)), int(m.group(3)), m.group(4)))
    raw.sort(key=lambda x: x[0])
    if not raw:
        return []
    t0 = raw[0][0]
    entries = []
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
    """Add capture number suffix to phase names for multi-capture runs."""
    capture_re = re.compile(r"=== Capture (\d+)/(\d+) ===")

    markers = []
    for t, cpu, tid, phase, msg in entries:
        m = capture_re.search(msg)
        if m:
            markers.append((t, int(m.group(1)), int(m.group(2)), tid))

    if not markers or markers[0][2] <= 1:
        return entries

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

    thread_capture = {}
    for t, cpu, tid, phase, msg in entries:
        if tid != main_tid and tid not in thread_capture:
            thread_capture[tid] = capture_at(t)

    result = []
    main_cap = 0
    for t, cpu, tid, phase, msg in entries:
        m = capture_re.search(msg)
        if m and tid == main_tid:
            main_cap = int(m.group(1))

        if phase is not None:
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
    spans = []
    active = {}
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
    for tid, (phase, ts, te) in active.items():
        spans.append((phase, tid, ts, te))
    return spans

# ---------------------------------------------------------------------------
# Resource CSV parsing
# ---------------------------------------------------------------------------
def parse_resource_csv(csv_path):
    data = {"epoch": [], "cpu0": [], "cpu1": [], "mem_pct": []}
    prev = None
    first_epoch = None
    with open(csv_path) as f:
        reader = csv.DictReader(f)
        for row in reader:
            try:
                epoch = int(row["epoch"])
                c0_active = int(row["cpu0_user"]) + int(row["cpu0_nice"]) + int(row["cpu0_system"])
                c0_total  = c0_active + int(row["cpu0_idle"]) + int(row["cpu0_iowait"])
                c1_active = int(row["cpu1_user"]) + int(row["cpu1_nice"]) + int(row["cpu1_system"])
                c1_total  = c1_active + int(row["cpu1_idle"]) + int(row["cpu1_iowait"])
                mem_total = int(row["mem_total_kb"])
                mem_avail = int(row["mem_available_kb"])
            except (KeyError, ValueError):
                continue
            mem_pct = 100.0 * (mem_total - mem_avail) / mem_total if mem_total > 0 else 0
            if prev is None:
                # Seed t=0 with first memory reading; CPU needs a diff so use 0%
                first_epoch = epoch
                data["epoch"].append(epoch)
                data["cpu0"].append(0.0)
                data["cpu1"].append(0.0)
                data["mem_pct"].append(mem_pct)
            else:
                d0a = c0_active - prev[0]; d0t = c0_total - prev[1]
                d1a = c1_active - prev[2]; d1t = c1_total - prev[3]
                data["epoch"].append(epoch)
                data["cpu0"].append(100.0 * d0a / d0t if d0t > 0 else 0)
                data["cpu1"].append(100.0 * d1a / d1t if d1t > 0 else 0)
                data["mem_pct"].append(mem_pct)
            prev = (c0_active, c0_total, c1_active, c1_total)
    return data

# ---------------------------------------------------------------------------
# Combined plot
# ---------------------------------------------------------------------------
def plot_combined(run_dir, output_path, title, x_max=None):
    csv_path = os.path.join(run_dir, "resource.csv")
    if not os.path.exists(csv_path):
        print(f"  Skipping {run_dir}: no resource.csv")
        return

    res = parse_resource_csv(csv_path)
    if not res["epoch"]:
        print(f"  Skipping {run_dir}: empty resource.csv")
        return

    # Collect log files
    log_files = []
    main_log = os.path.join(run_dir, "pretty-doomed.log")
    if os.path.exists(main_log):
        log_files.append(main_log)
    for cap in sorted(Path(run_dir).glob("capture-*")):
        cap_log = cap / "run.log"
        if cap_log.exists():
            log_files.append(str(cap_log))

    entries = parse_log(log_files) if log_files else []
    entries = annotate_capture_numbers(entries)
    spans = build_phase_spans(entries)

    # Time axes
    t0_res = res["epoch"][0]
    t_res = [e - t0_res for e in res["epoch"]]

    # Identify threads for Gantt
    tids = sorted(set(e[2] for e in entries)) if entries else []
    tid_labels = {}
    for tid in tids:
        cpus = [e[1] for e in entries if e[2] == tid]
        main_cpu = max(set(cpus), key=cpus.count)
        tid_labels[tid] = f"CPU{main_cpu} / Thread {tid}"

    # Move capture #N from bar labels to thread labels where possible
    spans, tid_labels = relocate_capture_labels(spans, tid_labels)

    tid_y = {tid: i for i, tid in enumerate(tids)}

    log_max = max((e[0] for e in entries), default=0)
    t_max = log_max if log_max > 0 else max(t_res, default=0)
    if x_max is not None:
        t_max = x_max

    n_threads = max(len(tids), 1)
    gantt_height = max(1.5, n_threads * 0.9 + 0.5)

    fig, (ax_gantt, ax_cpu, ax_mem) = plt.subplots(
        3, 1, figsize=(16, gantt_height + 5.5), sharex=True,
        gridspec_kw={"height_ratios": [gantt_height, 2.5, 1.5]},
    )
    fig.suptitle(title, fontsize=14, fontweight="bold", y=0.98)

    # --- Gantt timeline ---
    LABEL_PHASES = {"STT Model Load", "SDR Capture", "STT Inference", "DOOM", "Postcard"}
    bar_height = 0.55
    for phase, tid, ts, te in spans:
        color = PHASE_COLORS.get(phase_base(phase), "#CCCCCC")
        duration = max(te - ts, 0.3)
        y = tid_y.get(tid, 0)
        ax_gantt.barh(y, duration, left=ts, height=bar_height, color=color,
                      edgecolor="white", linewidth=0.5, alpha=0.85)
        # Label inside bar: phase name on line 1, duration on line 2
        if (te - ts) > t_max * 0.07:
            mid = (ts + te) / 2
            label = phase
            if phase_base(phase) in LABEL_PHASES and (te - ts) > 0.5:
                label += f"\n{te - ts:.1f}s"
            ax_gantt.text(mid, y, label, ha="center", va="center",
                          fontsize=6, fontweight="bold", color="white",
                          clip_on=True, alpha=0.9)

    # CPU migration markers
    prev_cpu = {}
    for t, cpu, tid, phase, msg in entries:
        if tid in prev_cpu and prev_cpu[tid] != cpu:
            y = tid_y.get(tid, 0)
            ax_gantt.plot(t, y, "k|", markersize=8, markeredgewidth=1.5, alpha=0.4)
        prev_cpu[tid] = cpu

    ax_gantt.set_yticks(range(len(tids)))
    ax_gantt.set_yticklabels([tid_labels[tid] for tid in tids], fontsize=9)
    ax_gantt.set_ylabel("Threads", fontsize=10)
    ax_gantt.invert_yaxis()
    ax_gantt.grid(axis="x", alpha=0.3)
    ax_gantt.set_xlim(-0.5, t_max + 0.5)

    # --- CPU usage ---
    ax_cpu.fill_between(t_res, res["cpu0"], alpha=0.3, color="#E74C3C")
    ax_cpu.fill_between(t_res, res["cpu1"], alpha=0.3, color="#F39C12")
    ax_cpu.plot(t_res, res["cpu0"], color="#E74C3C", linewidth=1.3, label="CPU0", alpha=0.9)
    ax_cpu.plot(t_res, res["cpu1"], color="#F39C12", linewidth=1.3, label="CPU1", alpha=0.9)
    ax_cpu.set_ylabel("CPU Usage (%)", fontsize=10)
    ax_cpu.set_ylim(0, 110)
    ax_cpu.legend(loc="upper left", bbox_to_anchor=(1.01, 0.85),
                  fontsize=8, framealpha=0.9)
    ax_cpu.grid(True, alpha=0.3)

    # --- Memory ---
    ax_mem.fill_between(t_res, res["mem_pct"], alpha=0.3, color="#8B0000")
    ax_mem.plot(t_res, res["mem_pct"], color="#8B0000", linewidth=1.3, label="Memory Used")
    ax_mem.set_ylabel("Memory (%)", fontsize=10)
    ax_mem.set_xlabel("Time (seconds)", fontsize=10)
    ax_mem.set_ylim(0, max(max(res["mem_pct"]) * 1.5, 30))
    ax_mem.legend(loc="upper left", bbox_to_anchor=(1.01, 0.85),
                  fontsize=8, framealpha=0.9)
    ax_mem.grid(True, alpha=0.3)

    # --- Shared legend (consolidated by base phase, no durations) ---
    seen_bases = set(phase_base(s[0]) for s in spans)
    handles = []
    for phase in PHASE_ORDER:
        if phase in seen_bases:
            handles.append(mpatches.Patch(color=PHASE_COLORS[phase], label=phase))
    if handles:
        ax_gantt.legend(handles=handles, loc="upper left", bbox_to_anchor=(1.01, 1),
                        fontsize=7.5, framealpha=0.9)

    plt.tight_layout(rect=[0, 0, 0.88, 0.96])
    fig.savefig(output_path, dpi=150, bbox_inches="tight")
    plt.close(fig)
    print(f"  Saved: {output_path}")


# ---------------------------------------------------------------------------
# Standalone resource plot (CPU + memory with phase strip)
# ---------------------------------------------------------------------------
def plot_standalone(run_dir, output_path, title, x_max=None):
    csv_path = os.path.join(run_dir, "resource.csv")
    if not os.path.exists(csv_path):
        print(f"  Skipping {run_dir}: no resource.csv")
        return

    res = parse_resource_csv(csv_path)
    if not res["epoch"]:
        print(f"  Skipping {run_dir}: empty resource.csv")
        return

    # Collect log files
    log_files = []
    main_log = os.path.join(run_dir, "pretty-doomed.log")
    if os.path.exists(main_log):
        log_files.append(main_log)
    for cap in sorted(Path(run_dir).glob("capture-*")):
        cap_log = cap / "run.log"
        if cap_log.exists():
            log_files.append(str(cap_log))

    entries = parse_log(log_files) if log_files else []
    entries = annotate_capture_numbers(entries)
    spans = build_phase_spans(entries)

    t0_res = res["epoch"][0]
    t_res = [e - t0_res for e in res["epoch"]]

    log_max = max((e[0] for e in entries), default=0)
    t_max_val = log_max if log_max > 0 else max(t_res, default=0)
    if x_max is not None:
        t_max_val = x_max

    # Group spans by thread for the phase strip
    tids = sorted(set(s[1] for s in spans)) if spans else []
    tid_to_row = {tid: i for i, tid in enumerate(tids)}
    n_rows = max(len(tids), 1)

    # Build thread labels
    tid_labels = {}
    for tid in tids:
        cpus = [e[1] for e in entries if e[2] == tid]
        main_cpu = max(set(cpus), key=cpus.count) if cpus else 0
        tid_labels[tid] = f"c{main_cpu}/t{tid}"

    # Move capture #N from bar labels to thread labels where possible
    spans, tid_labels = relocate_capture_labels(spans, tid_labels)
    # Rebuild tid ordering after span changes
    tids = sorted(set(s[1] for s in spans)) if spans else []
    tid_to_row = {tid: i for i, tid in enumerate(tids)}
    n_rows = max(len(tids), 1)

    strip_height = max(0.8, n_rows * 0.4 + 0.3)

    fig, (ax_strip, ax_cpu, ax_mem) = plt.subplots(
        3, 1, figsize=(16, strip_height + 5.5), sharex=True,
        gridspec_kw={"height_ratios": [strip_height, 3, 1.5]},
    )
    fig.suptitle(title, fontsize=14, fontweight="bold", y=0.98)

    # --- Phase strip: thin bars per thread ---
    LABEL_PHASES = {"STT Model Load", "SDR Capture", "STT Inference", "DOOM", "Postcard"}
    bar_h = 0.7
    for phase, tid, ts, te in spans:
        color = PHASE_COLORS.get(phase_base(phase), "#CCCCCC")
        row = tid_to_row.get(tid, 0)
        duration = max(te - ts, 0.3)
        ax_strip.barh(row, duration, left=ts, height=bar_h, color=color,
                       edgecolor="white", linewidth=0.5, alpha=0.85)
        # Label inside bar: phase name on line 1, duration on line 2
        if (te - ts) > t_max_val * 0.04:
            mid = (ts + te) / 2
            label = phase
            if phase_base(phase) in LABEL_PHASES and (te - ts) > 0.5:
                label += f"\n{te - ts:.1f}s"
            ax_strip.text(mid, row, label, ha="center", va="center",
                          fontsize=6, fontweight="bold", color="white",
                          clip_on=True, alpha=0.9)

    ax_strip.set_yticks(range(n_rows))
    ax_strip.set_yticklabels([tid_labels.get(tid, "") for tid in tids], fontsize=8)
    ax_strip.set_ylabel("Threads", fontsize=9)
    ax_strip.invert_yaxis()
    ax_strip.set_xlim(-0.5, t_max_val + 0.5)
    ax_strip.grid(axis="x", alpha=0.3)

    # --- CPU plot (no phase color bands — CPU allocation only) ---
    ax_cpu.fill_between(t_res, res["cpu0"], alpha=0.3, color="#E74C3C", label="CPU0")
    ax_cpu.fill_between(t_res, res["cpu1"], alpha=0.3, color="#F39C12", label="CPU1")
    ax_cpu.plot(t_res, res["cpu0"], color="#E74C3C", linewidth=1.2, alpha=0.9)
    ax_cpu.plot(t_res, res["cpu1"], color="#F39C12", linewidth=1.2, alpha=0.9)
    ax_cpu.set_ylabel("CPU Usage (%)", fontsize=10)
    ax_cpu.set_ylim(0, 110)
    ax_cpu.legend(loc="upper left", bbox_to_anchor=(1.01, 0.85),
                  fontsize=8, framealpha=0.9)
    ax_cpu.grid(True, alpha=0.3)

    # --- Memory ---
    ax_mem.fill_between(t_res, res["mem_pct"], alpha=0.3, color="#8B0000")
    ax_mem.plot(t_res, res["mem_pct"], color="#8B0000", linewidth=1.2, label="Memory Used")
    ax_mem.set_ylabel("Memory (%)", fontsize=10)
    ax_mem.set_xlabel("Time (seconds)", fontsize=10)
    ax_mem.set_ylim(0, max(max(res["mem_pct"]) * 1.5, 30))
    ax_mem.legend(loc="upper left", bbox_to_anchor=(1.01, 0.85),
                  fontsize=8, framealpha=0.9)
    ax_mem.grid(True, alpha=0.3)

    # --- Legend (consolidated by base phase, no durations) ---
    seen_bases = set(phase_base(s[0]) for s in spans)
    handles = []
    for phase in PHASE_ORDER:
        if phase in seen_bases:
            handles.append(mpatches.Patch(color=PHASE_COLORS[phase], label=phase))
    if handles:
        ax_strip.legend(handles=handles, loc="upper left", bbox_to_anchor=(1.01, 1),
                        fontsize=7.5, framealpha=0.9)

    plt.tight_layout(rect=[0, 0, 0.88, 0.96])
    fig.savefig(output_path, dpi=150, bbox_inches="tight")
    plt.close(fig)
    print(f"  Saved: {output_path}")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
RUN_TITLES = {
    "run-00001": "Run 1: SDR Capture (sequential, stt_concurrent_load=false)",
    "run-00002": "Run 2: SDR Capture (background, stt_concurrent_load=false)",
    "run-00003": "Run 3: SDR Capture (background, stt_concurrent_load=true)",
}

def main():
    parser = argparse.ArgumentParser(description="Combined timeline + resource plot")
    parser.add_argument("run_dir", nargs="?", help="Single run directory")
    parser.add_argument("--batch", action="store_true", help="Process all runs in toGround/")
    parser.add_argument("--standalone", action="store_true",
                        help="Also generate standalone resource plots (phase strip + CPU + memory)")
    parser.add_argument("--input-dir",
                        help="Directory containing run-* dirs (default: toGround/)")
    parser.add_argument("--output-dir", help="Output directory for plots")
    args = parser.parse_args()

    if args.batch:
        toground = Path(args.input_dir) if args.input_dir else Path("toGround")
        runs = sorted(p for p in toground.glob("run-*") if p.is_dir())
        if not runs:
            print("No runs found in toGround/"); sys.exit(1)

        if args.output_dir:
            out_dir = Path(args.output_dir)
        else:
            plots_base = Path("artifacts/plots/local")
            existing = sorted(plots_base.glob("[0-9]*"))
            next_id = int(existing[-1].name) + 1 if existing else 1
            out_dir = plots_base / f"{next_id:03d}"
        out_dir.mkdir(parents=True, exist_ok=True)

        # First pass: find global x_max
        all_entries = []
        all_res_t = []
        for run in runs:
            csv_path = run / "resource.csv"
            if csv_path.exists():
                res = parse_resource_csv(str(csv_path))
                if res["epoch"]:
                    t0 = res["epoch"][0]
                    all_res_t.extend(e - t0 for e in res["epoch"])
            log_files = []
            main_log = run / "pretty-doomed.log"
            if main_log.exists():
                log_files.append(str(main_log))
            for cap in sorted(run.glob("capture-*")):
                cap_log = cap / "run.log"
                if cap_log.exists():
                    log_files.append(str(cap_log))
            if log_files:
                entries = parse_log(log_files)
                all_entries.extend(entries)

        global_x_max = max((e[0] for e in all_entries), default=0)
        if global_x_max == 0:
            global_x_max = max(all_res_t, default=0)

        for run in runs:
            name = run.name
            title = RUN_TITLES.get(name, name)
            out_path = out_dir / f"{name}-resource.png"
            plot_combined(str(run), str(out_path), title, x_max=global_x_max)
            if args.standalone:
                sa_path = out_dir / f"{name}-resource-standalone.png"
                plot_standalone(str(run), str(sa_path), title, x_max=global_x_max)

    elif args.run_dir:
        out_dir = Path(args.output_dir) if args.output_dir else Path(".")
        out_dir.mkdir(parents=True, exist_ok=True)
        name = os.path.basename(args.run_dir)
        title = RUN_TITLES.get(name, name)
        out_path = out_dir / f"{name}-resource.png"
        plot_combined(args.run_dir, str(out_path), title)
        if args.standalone:
            sa_path = out_dir / f"{name}-resource-standalone.png"
            plot_standalone(args.run_dir, str(sa_path), title)
    else:
        parser.print_help()

if __name__ == "__main__":
    main()
