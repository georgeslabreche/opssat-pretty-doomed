#!/usr/bin/env python3
"""Report of one distress run on the EM or on the SEPP emulator (issue #142).

Reads a run directory (toGround/distress-NNNNN/ of the distress ./run) and the
per-clip scores of score.py, and writes pretty-doomed/docs/distress/reports/<label>/:

  REPORT.md               run data, resource use, speed, and accuracy tables
  timeline-and-resource.png  thread timeline, per-core CPU, and memory (plot_resource.py, as for the DOOM runs)
  peak_memory.png         peak memory (VmHWM) of each model, and the free memory at the start
  rtf.png                 real-time factor of each clip, per model
  run_time.png            run time of each model: model load, transcription, and the rest
  report_cards/<clip>/<model>.png   every report card of the run, made on board
  accuracy.png            ship-side WER and recall, this run and the ground binary

The resource data is the same as for the DOOM runs: resource.csv has per-core
CPU and memory every second, with the columns of the run script.

Usage (from tools/distress), after import_em.py and score.py:
  python3 import_em.py <run_dir> --label emu --machine "..."
  python3 score.py
  python3 report.py <run_dir> --label emu [--title "..."]
"""

import argparse
import csv
import datetime
import glob
import os
import re
import shutil
import statistics
import subprocess
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import matplotlib.ticker  # noqa: E402

import mem_headroom  # noqa: E402
import paths  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))

# Reference palette of the dataviz method: categorical slots 1-4, fixed order.
SERIES = ["#2a78d6", "#eb6834", "#1baf7a", "#eda100"]
SURFACE = "#fcfcfb"
INK = "#0b0b0b"
INK_2 = "#52514e"
GRID = "#e4e3df"
BAND = "#f0efec"

LOG_TS = re.compile(r"^\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})")
TS_FORMAT = "%Y-%m-%d %H:%M:%S"


def read_tsv(path):
    with open(path, encoding="utf-8") as f:
        return list(csv.DictReader(f, delimiter="\t"))


def read_manifest(path):
    """Return the key: value lines of manifest.txt."""
    data = {}
    if os.path.exists(path):
        with open(path, encoding="utf-8") as f:
            for line in f:
                key, sep, value = line.partition(":")
                if sep and key and " " not in key.strip() and key.strip() not in data:
                    data[key.strip()] = value.strip()
    return data


def log_span(path):
    """Return the first and the last timestamp of a pretty-doomed.log, or None."""
    stamps = []
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = LOG_TS.match(line)
            if m:
                stamps.append(datetime.datetime.strptime(m.group(1), TS_FORMAT))
    return (stamps[0], stamps[-1]) if stamps else None


def read_resource(path):
    """Return samples of time, CPU % per core, and MemAvailable (MiB)."""
    with open(path, encoding="utf-8") as f:
        rows = [r for r in csv.DictReader(f) if (r.get("mem_available_kb") or "").strip().isdigit()]
    out = {"t": [], "cpu0": [], "cpu1": [], "avail": []}
    prev = None
    for r in rows:
        t = datetime.datetime.strptime(r["timestamp"], TS_FORMAT)
        cores = []
        for c in ("cpu0", "cpu1"):
            busy = sum(int(r[f"{c}_{k}"]) for k in ("user", "nice", "system"))
            idle = sum(int(r[f"{c}_{k}"]) for k in ("idle", "iowait"))
            cores.append((busy, idle))
        if prev is not None:
            out["t"].append(t)
            for name, (busy, idle), (pbusy, pidle) in zip(("cpu0", "cpu1"), cores, prev):
                total = (busy - pbusy) + (idle - pidle)
                out[name].append(100.0 * (busy - pbusy) / total if total > 0 else 0.0)
            out["avail"].append(int(r["mem_available_kb"]) / 1024)
        prev = cores
    out["idle_avail"] = int(rows[0]["mem_available_kb"]) / 1024 if rows else None
    out["total"] = int(rows[0]["mem_total_kb"]) / 1024 if rows else None
    return out


def style(ax):
    ax.set_facecolor(SURFACE)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    for side in ("left", "bottom"):
        ax.spines[side].set_color(GRID)
    ax.tick_params(colors=INK_2, labelsize=8)
    ax.grid(color=GRID, linewidth=0.6)
    ax.set_axisbelow(True)


def new_figure(rows, height):
    fig, axes = plt.subplots(rows, 1, figsize=(9, height), facecolor=SURFACE, squeeze=False)
    for ax in axes[:, 0]:
        style(ax)
    return fig, axes[:, 0]


def save(fig, path):
    fig.savefig(path, dpi=150, bbox_inches="tight", facecolor=SURFACE)
    plt.close(fig)


def plot_timeline_and_resource(run_dir, out_dir):
    """Thread timeline with per-core CPU and memory: plot_resource.py, the figure of the DOOM runs."""
    script = os.path.join(HERE, "..", "..", "pretty-doomed", "scripts", "plots", "plot_resource.py")
    done = subprocess.run([sys.executable, script, run_dir, "--output-dir", out_dir],
                          capture_output=True, text=True)
    produced = os.path.join(out_dir, os.path.basename(os.path.normpath(run_dir)) + "-timeline-and-resource.png")
    target = os.path.join(out_dir, "timeline-and-resource.png")
    if done.returncode != 0 or not os.path.exists(produced):
        return None
    os.replace(produced, target)
    return os.path.basename(target)


def plot_peak_memory(configs, free_ref, path):
    names = list(configs)
    peaks = [configs[n]["peak_mib"] or 0 for n in names]
    fig, (ax,) = new_figure(1, 0.5 + 0.45 * len(names))
    y = list(range(len(names)))[::-1]
    ax.barh(y, peaks, color=SERIES[0], height=0.55, edgecolor=SURFACE, linewidth=2)
    for yi, p in zip(y, peaks):
        ax.text(p + 8, yi, f"{p:.0f} MiB", va="center", fontsize=8, color=INK)
    idle_avail = free_ref[0]
    ax.axvline(idle_avail, color=INK_2, linewidth=1, linestyle=(0, (4, 3)))
    ax.text(idle_avail, len(names) - 0.45, f"{free_ref[1]}\n{idle_avail:.0f} MiB", ha="center",
            va="bottom", fontsize=7, color=INK_2)
    ax.set_yticks(y)
    ax.set_yticklabels(names, fontsize=8, color=INK)
    ax.set_xlim(0, max(peaks + [idle_avail]) * 1.15)
    ax.set_xlabel("Peak memory of the process, VmHWM (MiB)", color=INK_2, fontsize=8)
    ax.grid(axis="y", visible=False)
    ax.set_title("Peak memory per model", loc="left", fontsize=10, color=INK, pad=22)
    save(fig, path)


def plot_rtf(configs, path):
    names = list(configs)
    fig, (ax,) = new_figure(1, 0.6 + 0.5 * len(names))
    for i, name in enumerate(names):
        yi = len(names) - 1 - i
        rtfs = [c["rtf"] for c in configs[name]["clips"]]
        ax.scatter(rtfs, [yi] * len(rtfs), s=36, color=SERIES[0], edgecolor=SURFACE,
                   linewidth=1.5, zorder=3)
        med = statistics.median(rtfs)
        ax.plot([med, med], [yi - 0.3, yi + 0.3], color=INK, linewidth=2, zorder=4)
        ax.text(med, yi + 0.33, f"median {med:.2f}", ha="center", va="bottom", fontsize=7, color=INK)
    ax.axvline(1, color=INK_2, linewidth=0.8, linestyle=(0, (4, 3)))
    ax.set_yticks(range(len(names)))
    ax.set_yticklabels(names[::-1], fontsize=8, color=INK)
    ax.set_ylim(-0.6, len(names) - 0.2)
    ax.set_xscale("log")
    ax.xaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(lambda v, _: f"{v:g}"))
    ax.tick_params(axis="x", which="minor", labelsize=8, colors=INK_2)
    ax.xaxis.set_minor_formatter(matplotlib.ticker.FuncFormatter(lambda v, _: f"{v:g}" if v in (2, 3, 5, 20, 30, 50) else ""))
    ax.set_xlabel("Real-time factor per clip (STT time / audio time, log scale; 1 = real time)",
                  color=INK_2, fontsize=8)
    ax.grid(axis="y", visible=False)
    ax.set_title("Speed per clip", loc="left", fontsize=10, color=INK)
    save(fig, path)


def plot_run_time(configs, path):
    """Run time of each model, split into model load, transcription, and the rest (cards, detection, exit)."""
    names = list(configs)
    fig, (ax,) = new_figure(1, 0.9 + 0.5 * len(names))
    y = list(range(len(names)))[::-1]
    parts = (("Model load", SERIES[0]), ("Transcription", SERIES[1]), ("Other", GRID))
    for yi, name in zip(y, names):
        c = configs[name]
        load = c["load_s"]
        stt = sum(x["stt_s"] for x in c["clips"])
        total = c["seconds"] or load + stt
        left = 0
        for (part, color), width in zip(parts, (load, stt, max(total - load - stt, 0))):
            ax.barh(yi, width, left=left, color=color, height=0.55, edgecolor=SURFACE, linewidth=2,
                    label=part if yi == y[0] else None)
            left += width
        ax.text(left + 1, yi, f"{total:.0f} s", va="center", fontsize=8, color=INK)
    ax.set_yticks(y)
    ax.set_yticklabels(names, fontsize=8, color=INK)
    ax.set_xlim(0, max((configs[n]["seconds"] or 0) for n in names) * 1.12 + 1)
    ax.set_xlabel("Run time of the model process (s)", color=INK_2, fontsize=8)
    ax.grid(axis="y", visible=False)
    ax.legend(loc="lower right", bbox_to_anchor=(1.0, 1.0), ncol=3, fontsize=7, frameon=False)
    ax.set_title("Run time per model", loc="left", fontsize=10, color=INK, pad=14)
    save(fig, path)


def plot_accuracy(rows, label, ground_label, n_clips, path):
    """Ship-side WER and recall per model: this run against the ground binary."""
    names = [r["config"] for r in rows]
    fig, (ax_wer, ax_rec) = new_figure(2, 1.2 + 0.9 * len(names))
    y = list(range(len(names)))[::-1]
    wer_max = max([1.0] + [v * 1.1 for r in rows for v in r["wer"] if v is not None])
    for ax, key, xlabel, xmax in ((ax_wer, "wer", "Ship-side WER (lower is better)", wer_max),
                                  (ax_rec, "recall", "Ship-side recall (clips with a distress phrase)", n_clips + 0.9)):
        for j, (who, color, offset) in enumerate(((label, SERIES[0], 0.14), (ground_label, SERIES[1], -0.14))):
            vals = [r[key][j] for r in rows]
            pts = [(v, yi + offset) for v, yi in zip(vals, y) if v is not None]
            ax.scatter([p[0] for p in pts], [p[1] for p in pts], s=40, color=color, edgecolor=SURFACE,
                       linewidth=1.5, zorder=3, label=who)
            for v, yy in pts:
                ax.text(v, yy + 0.08, f" {v:.3f}" if key == "wer" else f" {v:.0f}", fontsize=7, color=INK,
                        va="bottom")
        ax.set_yticks(y)
        ax.set_yticklabels(names, fontsize=8, color=INK)
        ax.set_xlim(0, xmax)
        if key == "recall":
            ax.xaxis.set_major_locator(matplotlib.ticker.MaxNLocator(integer=True))
        ax.set_xlabel(xlabel, color=INK_2, fontsize=8)
        ax.grid(axis="y", visible=False)
    handles, labels = ax_wer.get_legend_handles_labels()
    ax_wer.legend(handles, labels, loc="lower right", bbox_to_anchor=(1.0, 1.0), ncol=2, fontsize=7, frameon=False)
    ax_wer.set_title("Accuracy: this run and the ground binary", loc="left", fontsize=10, color=INK)
    fig.tight_layout()
    save(fig, path)


def subset_scores(per_clip, model, clips):
    """Pooled ship-side WER and recall of one results/<model> on the given clips (per_clip.tsv of score.py).

    The pooled WER is the sum of the errors over the sum of the reference
    words, without the clips with wer_excluded=yes.
    """
    rows = [r for r in per_clip if r["model"] == model and r["clip"] in clips]
    if not rows:
        return {"wer": None, "recall": None, "recall_text": "", "phrases": ""}
    words = errors = 0.0
    for r in rows:
        if r["wer_excluded"] == "yes" or not r["wer_ship"]:
            continue
        words += int(r["ship_words"])
        errors += round(float(r["wer_ship"]) * int(r["ship_words"]))  # whole errors
    hits = sum(r["detected_ship"] == "yes" for r in rows)
    phrases = sorted({p for r in rows for p in r["phrases_ship"].split(",") if p})
    return {"wer": errors / words if words else None, "recall": hits,
            "recall_text": f"{hits}/{len(rows)}", "phrases": ", ".join(phrases)}


def duration(seconds):
    return f"{seconds:.0f} s" if seconds < 120 else f"{seconds / 60:.0f} min"


def fmt3(value):
    return "" if value is None else f"{value:.3f}"


def to_float(value):
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("run_dir", help="toGround/distress-NNNNN directory")
    parser.add_argument("--label", required=True, help="label of the run in results/ (as in import_em.py)")
    parser.add_argument("--ground-label", default="binary", help="label of the ground baseline (default: binary)")
    parser.add_argument("--title", help="report title")
    parser.add_argument("--out", help="output directory (default: reports/<label>)")
    parser.add_argument("--results", default=paths.RESULTS,
                        help="results directory with per_clip.tsv of score.py (default: results)")
    parser.add_argument("--card-clip",
                        help="show the report cards of this clip only (default: all the cards of the run)")
    parser.add_argument("--free-from", choices=sorted(mem_headroom.PATTERNS),
                        help="use the median idle free memory of the past runs of this machine (mem_headroom.py) as "
                             "the reference, not MemAvailable at the start of this run. Use it for an emulator run, "
                             "where resource.csv shows the Docker VM and not the EM")
    args = parser.parse_args()

    out = args.out or os.path.join(paths.REPORTS, args.label)
    os.makedirs(out, exist_ok=True)
    manifest = read_manifest(os.path.join(args.run_dir, "manifest.txt"))
    exits = {r["model"]: r for r in read_tsv(os.path.join(args.run_dir, "exit.txt"))}
    res = read_resource(os.path.join(args.run_dir, "resource.csv"))
    if not res["t"]:
        sys.exit("resource.csv has no samples.")

    configs, spans = {}, {}
    for name in manifest.get("models", "").split() or list(exits):
        model_dir = os.path.join(args.run_dir, name)
        log = os.path.join(model_dir, "pretty-doomed.log")
        if not os.path.exists(log):
            continue
        stt = read_tsv(os.path.join(model_dir, "stt.tsv"))[0] if os.path.exists(os.path.join(model_dir, "stt.tsv")) else {}
        timing = read_tsv(os.path.join(model_dir, "timing.tsv")) if os.path.exists(os.path.join(model_dir, "timing.tsv")) else []
        span = log_span(log)
        with open(log, encoding="utf-8", errors="replace") as f:
            card_ms = [int(m.group(1)) for m in re.finditer(r"Report card: \S+ \((\d+) ms\)", f.read())]
        if span:
            spans[name] = span
        peak_kb = to_float(stt.get("peak_rss_kb"))
        mem_in_span = [a for t, a in zip(res["t"], res["avail"]) if span and span[0] <= t <= span[1]]
        configs[name] = {
            "exit": exits.get(name, {}).get("exit", ""),
            "seconds": to_float(exits.get(name, {}).get("seconds")),
            "load_s": (to_float(stt.get("load_ms")) or 0) / 1000,
            "peak_mib": peak_kb / 1024 if peak_kb else None,
            "min_avail": min(mem_in_span) if mem_in_span else None,
            "window_s": stt.get("window_s", ""),
            "card_ms": f"{statistics.median(card_ms):.0f} ms" if card_ms else "",
            "decoding": stt.get("decoding", ""),
            "clips": [{"clip": r["clip"], "audio_s": float(r["audio_s"]), "stt_s": float(r["stt_s"]),
                       "rtf": float(r["rtf"]), "detected": r["detected"]} for r in timing],
        }
    if not configs:
        sys.exit("No config output in the run directory.")

    if args.free_from:
        history = mem_headroom.idle_free(args.free_from)
        if not history:
            sys.exit(f"No past {args.free_from} runs with memory data.")
        free_ref = (statistics.median(history), f"{args.free_from} free memory, median of {len(history)} runs")
    else:
        free_ref = (res["idle_avail"], "free at start")
    timeline = plot_timeline_and_resource(args.run_dir, out)
    # Every report card of the run, by clip: report_cards/<clip>/<config>.png.
    # --card-clip keeps the cards of one clip only.
    run_clip_names = list(dict.fromkeys(x["clip"] for c in configs.values() for x in c["clips"]))
    card_clips = [args.card_clip] if args.card_clip else run_clip_names
    cards = {}  # clip -> [(config, relative path)]
    for clip in card_clips:
        for name in configs:
            src = os.path.join(args.run_dir, name, clip, "report_card.png")
            if os.path.exists(src):
                rel = os.path.join("report_cards", clip, f"{name}.png")
                os.makedirs(os.path.join(out, "report_cards", clip), exist_ok=True)
                shutil.copyfile(src, os.path.join(out, rel))
                cards.setdefault(clip, []).append((name, rel))
    for name in configs:
        configs[name]["cards"] = len(glob.glob(os.path.join(args.run_dir, name, "*", "report_card.png")))
    plot_peak_memory(configs, free_ref, os.path.join(out, "peak_memory.png"))
    plot_rtf(configs, os.path.join(out, "rtf.png"))
    plot_run_time(configs, os.path.join(out, "run_time.png"))
    # Accuracy on the clips of this run only, for this run and the ground
    # baseline alike, so that a run of a few clips is not scored on 13 clips.
    per_clip = read_tsv(os.path.join(args.results, "per_clip.tsv"))
    run_clips = {x["clip"] for c in configs.values() for x in c["clips"]}
    acc_rows = []
    for name in configs:
        acc_rows.append({
            "config": name,
            "run": subset_scores(per_clip, f"{args.label}-{name}", run_clips),
            "ground": subset_scores(per_clip, f"{args.ground_label}-{name}", run_clips),
        })
        acc_rows[-1]["wer"] = (acc_rows[-1]["run"]["wer"], acc_rows[-1]["ground"]["wer"])
        acc_rows[-1]["recall"] = (acc_rows[-1]["run"]["recall"], acc_rows[-1]["ground"]["recall"])
    plot_accuracy(acc_rows, args.label, args.ground_label, len(run_clips), os.path.join(out, "accuracy.png"))

    total_audio = sum(c["audio_s"] for c in next(iter(configs.values()))["clips"])
    run_hours = (res["t"][-1] - res["t"][0]).total_seconds() / 3600
    lines = [
        f"# {args.title or 'Distress call run ' + manifest.get('run', '')}",
        "",
        f"Generated by `report.py` from `{manifest.get('run', os.path.basename(args.run_dir))}`. "
        f"Accuracy from `score.py` (`results/{args.label}-<config>` against `results/{args.ground_label}-<config>`).",
        "",
        "## Run",
        "",
        "| Item | Value |",
        "|---|---|",
        f"| Package version | {manifest.get('version', '')} |",
        f"| Start (UTC) | {manifest.get('start_utc', '')} |",
        f"| End (UTC) | {manifest.get('end_utc', '')} |",
        f"| Run time | {run_hours:.2f} h |",
        f"| System | `{manifest.get('uname', '')}` |",
        f"| MemTotal | {res['total']:.0f} MiB |",
        f"| Free memory at start (MemAvailable) | {res['idle_avail']:.0f} MiB |",
        f"| Lowest free memory during the run | {min(res['avail']):.0f} MiB |",
        f"| Clips | {len(next(iter(configs.values()))['clips'])}, {total_audio:.0f} s of audio |",
        "",
        "## Resource use",
        "",
        "The resource data is the same as for the DOOM runs: `resource.csv` has per-core CPU and memory every second. "
        "The peak memory is the VmHWM of the pretty-doomed process, from `stt.tsv`.",
        "",
    ]
    if args.free_from:
        lines += [
            f"The reference free memory is the {free_ref[1]}, {free_ref[0]:.0f} MiB, measured by `mem_headroom.py` from "
            f"the `resource.csv` files of those runs (`--free-from {args.free_from}`). On the SEPP emulator, "
            "`resource.csv` shows the CPU and the memory of the Docker VM, with all of its containers, and not of the EM. "
            "Thus the CPU panel, the system memory line, MemTotal, and the free memory in the run table are not EM data. "
            "The thread timeline and the memory of the pretty-doomed process (`process.csv`) are valid. The process "
            "memory includes the memory of QEMU.",
            "",
        ]
    lines += [
        "Thread timeline, per-core CPU, and memory (`plot_resource.py`, the figure of the DOOM runs). Each model "
        "runs in its own process. Each thread row has the name of its model: the main thread transcribes the "
        "clips, and the report card thread draws the cards while the next clip is transcribed. The memory panel "
        "has the system memory (`resource.csv`) and the memory of the pretty-doomed process (`process.csv`).",
        "",
    ]
    lines += [f"![Thread timeline, CPU, and memory]({timeline})", ""] if timeline else ["No timeline: plot_resource.py failed.", ""]
    lines += [
        "![Peak memory per model](peak_memory.png)",
        "",
        f"| Model | Exit | Run time | Model load | Peak memory (VmHWM) | Margin to {free_ref[1]} ({free_ref[0]:.0f} MiB) | Lowest free memory |",
        "|---|---|---|---|---|---|---|",
    ]
    for name, c in configs.items():
        peak = f"{c['peak_mib']:.0f} MiB" if c["peak_mib"] else ""
        margin = f"{free_ref[0] - c['peak_mib']:.0f} MiB" if c["peak_mib"] else ""
        low = f"{c['min_avail']:.0f} MiB" if c["min_avail"] is not None else ""
        run_t = duration(c["seconds"]) if c["seconds"] is not None else ""
        lines.append(f"| `{name}` | {c['exit']} | {run_t} | {c['load_s']:.1f} s | {peak} | {margin} | {low} |")
    if cards:
        lines += [
            "",
            "## Report cards made on board",
            "",
            f"One PNG per clip and model, drawn by the pretty-doomed binary on board (`report_card=true`). "
            f"{sum(len(v) for v in cards.values())} cards below, by clip, in `report_cards/<clip>/<model>.png`.",
            "",
            "| Model | Cards | Card time, median |",
            "|---|---|---|",
        ]
        for name, c in configs.items():
            lines.append(f"| `{name}` | {c['cards']} | {c['card_ms']} |")
        lines.append("")
        for clip, items in cards.items():
            lines += [f"### {clip}", ""]
            for name, rel in items:
                lines += [f"![{clip}, {name}]({rel})", ""]
    lines += [
        "",
        "## Speed",
        "",
        "Run time of each model process: model load, transcription of all the clips, and the rest (report cards "
        "after the last clip, detection, model release, and exit).",
        "",
        "![Run time per model](run_time.png)",
        "",
        "![Speed per clip](rtf.png)",
        "",
        "| Model | Window | Decoding | Clips | Audio | STT time | Median RTF | Highest RTF |",
        "|---|---|---|---|---|---|---|---|",
    ]
    for name, c in configs.items():
        rtfs = [x["rtf"] for x in c["clips"]]
        lines.append(
            f"| `{name}` | {c['window_s']} s | {c['decoding']} | {len(c['clips'])} | "
            f"{sum(x['audio_s'] for x in c['clips']):.0f} s | {duration(sum(x['stt_s'] for x in c['clips']))} | "
            f"{statistics.median(rtfs):.2f} | {max(rtfs):.2f} |")
    lines += [
        "",
        "## Accuracy",
        "",
        "Ship-side WER uses the VESSEL and OTHER turns. Recall is the number of clips with a distress phrase in the "
        "ship-side speech. The scoring rules are in `tools/distress/README.md`.",
        "",
        "![Accuracy](accuracy.png)",
        "",
        f"Scored on the {len(run_clips)} clip(s) of this run, for this run and for `{args.ground_label}` alike.",
        "",
        f"| Model | WER, ship-side ({args.label}) | WER, ship-side ({args.ground_label}) | Recall ({args.label}) | "
        f"Recall ({args.ground_label}) | Phrases, ship-side ({args.label}) |",
        "|---|---|---|---|---|---|",
    ]
    for r in acc_rows:
        a, g = r["run"], r["ground"]
        lines.append(f"| `{r['config']}` | {fmt3(a['wer'])} | {fmt3(g['wer'])} | {a['recall_text']} | "
                     f"{g['recall_text']} | {a['phrases']} |")
    lines += [
        "",
        "## Per clip",
        "",
        "Real-time factor and on-board detection (`detected` in `timing.tsv`). The on-board detection accepts one "
        "approximate match, so it is for information only. The ground scorer gives the recall.",
        "",
        "| Clip | Audio | " + " | ".join(f"`{n}`" for n in configs) + " |",
        "|---|---|" + "---|" * len(configs),
    ]
    clips = [x["clip"] for x in next(iter(configs.values()))["clips"]]
    for clip in clips:
        cells = []
        audio = ""
        for c in configs.values():
            row = next((x for x in c["clips"] if x["clip"] == clip), None)
            if row:
                audio = f"{row['audio_s']:.0f} s"
                cells.append(f"{row['rtf']:.2f}, {row['detected']}")
            else:
                cells.append("not done")
        lines.append(f"| {clip} | {audio} | " + " | ".join(cells) + " |")
    with open(os.path.join(out, "REPORT.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print(f"Report: {os.path.join(out, 'REPORT.md')}")


if __name__ == "__main__":
    main()
