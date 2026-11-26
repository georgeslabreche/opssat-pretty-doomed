#!/usr/bin/env python3
"""Score the model outputs of transcribe.py against the reference transcripts.

The rules are in README.md, section "Scoring". The script writes:

    pretty-doomed/docs/distress/results/per_clip.tsv   one row per model and clip
    pretty-doomed/docs/distress/results/summary.tsv    one row per model
    pretty-doomed/docs/distress/results/RESULTS.md     the summary as a Markdown table

Usage (from tools/distress, after transcribe.py):

    python3 score.py
"""

import argparse
import csv
import glob
import json
import os
import statistics
import sys

import paths
import scoring

HERE = os.path.dirname(os.path.abspath(__file__))


def read_labels(path):
    """Read labels.tsv. Return {clip: row}."""
    with open(path, encoding="utf-8") as f:
        return {f"{r['id']}-{r['name']}": r for r in csv.DictReader(f, delimiter="\t")}


def score_clip(ref, hyp_text, phrases):
    """Score one model output against one reference."""
    hyp = scoring.normalize(hyp_text)
    counts = scoring.error_counts(ref.words, ref.speakers, hyp)
    ship_mask = [s in scoring.SHIP_SIDE for s in scoring.hyp_speakers(ref.words, ref.speakers, hyp)]
    whole = scoring.find_phrases(hyp, phrases)
    ship = scoring.find_phrases(hyp, phrases, allowed=ship_mask)
    return {
        "counts": counts,
        "wer_ship": scoring.wer(counts, scoring.SHIP_SIDE),
        "wer_all": scoring.wer(counts, scoring.SPEAKERS),
        "phrases_whole": whole,
        "phrases_ship": ship,
    }


def pooled_wer(rows, group):
    """Return the WER over all clips together: all errors divided by all reference words."""
    words = sum(r["counts"][s]["words"] for r in rows for s in group)
    errors = sum(r["counts"][s][k] for r in rows for s in group for k in ("sub", "del", "ins"))
    return errors / words if words else None


def fmt(value, digits=3):
    return "" if value is None else f"{value:.{digits}f}"


def write_tsv(path, rows, fields):
    with open(path, "w", encoding="utf-8", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fields, delimiter="\t", lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--results", default=paths.RESULTS, help="output directory of transcribe.py")
    args = parser.parse_args()

    refs = {r.clip: r for r in map(scoring.parse_reference, sorted(glob.glob(os.path.join(HERE, "reference", "*.txt"))))}
    unchecked = [c for c, r in refs.items() if r.status != "checked"]
    if unchecked:
        sys.exit(f"References not checked: {', '.join(unchecked)}")
    labels = read_labels(os.path.join(HERE, "labels.tsv"))
    phrases = scoring.load_phrases(os.path.join(HERE, "phrases.txt"))
    mayday_clips = [c for c in refs if labels[c]["says_mayday"] == "yes"]

    per_clip, summary = [], []
    for run_path in sorted(glob.glob(os.path.join(args.results, "*", "run.json"))):
        with open(run_path, encoding="utf-8") as f:
            run = json.load(f)
        model_dir = os.path.dirname(run_path)
        scored = {}
        done = 0
        for clip, ref in refs.items():
            # A clip without output (a run that stopped early) counts as a miss with an empty transcript.
            path = os.path.join(model_dir, clip + ".txt")
            text = ""
            if os.path.exists(path):
                done += 1
                with open(path, encoding="utf-8") as f:
                    text = f.read()
            scored[clip] = score_clip(ref, text, phrases)
            s = scored[clip]
            per_clip.append({
                "model": run["model"], "clip": clip,
                "wer_excluded": "yes" if ref.wer_excluded else "no",
                "ship_words": sum(s["counts"][k]["words"] for k in scoring.SHIP_SIDE),
                "all_words": len(ref.words),
                "wer_ship": fmt(s["wer_ship"]), "wer_all": fmt(s["wer_all"]),
                "detected_ship": "yes" if s["phrases_ship"] else "no",
                "detected_whole": "yes" if s["phrases_whole"] else "no",
                "phrases_ship": ",".join(s["phrases_ship"]),
                "phrases_whole": ",".join(s["phrases_whole"]),
                "rtf": run["clips"].get(clip, {}).get("rtf", ""),
            })
        wer_rows = [scored[c] for c, r in refs.items() if not r.wer_excluded]
        summary.append({
            "model": run["model"],
            "model_mb": run.get("model_mb") or "",
            "clips_done": f"{done}/{len(refs)}",
            "recall_ship": f"{sum(bool(s['phrases_ship']) for s in scored.values())}/{len(refs)}",
            "recall_whole": f"{sum(bool(s['phrases_whole']) for s in scored.values())}/{len(refs)}",
            "mayday_ship": f"{sum('mayday' in scored[c]['phrases_ship'] for c in mayday_clips)}/{len(mayday_clips)}",
            "wer_ship": fmt(pooled_wer(wer_rows, scoring.SHIP_SIDE)),
            "wer_all": fmt(pooled_wer(wer_rows, scoring.SPEAKERS)),
            "rtf_median": fmt(statistics.median(c["rtf"] for c in run["clips"].values())),
            "peak_rss_mb": run.get("peak_rss_mb") or "",
            "decoding": run["decoding"],
            "threads": run["threads"],
            "sherpa_onnx": run["sherpa_onnx"],
            "machine": run["machine"],
        })
    if not summary:
        sys.exit(f"No results in {args.results}. Run transcribe.py first.")

    write_tsv(os.path.join(args.results, "per_clip.tsv"), per_clip, list(per_clip[0]))
    write_tsv(os.path.join(args.results, "summary.tsv"), summary, list(summary[0]))
    n_wer = sum(not r.wer_excluded for r in refs.values())
    lines = [
        "# Distress call model comparison",
        "",
        f"Generated by `score.py` from the outputs of `transcribe.py`. {len(refs)} clips, all distress by source label. "
        f"The WER uses {n_wer} clips (the clips with `wer: exclude` are left out). {len(mayday_clips)} clips say \"mayday\".",
        "",
        "| Model | Clips done | Size (MB) | Recall, ship-side | Recall, whole clip | Mayday, ship-side | WER, ship-side | WER, all turns | Median RTF | Peak memory (MB) |",
        "|---|---|---|---|---|---|---|---|---|---|",
    ]
    for s in summary:
        lines.append(f"| {s['model']} | {s['clips_done']} | {s['model_mb']} | {s['recall_ship']} | {s['recall_whole']} | {s['mayday_ship']} | "
                     f"{s['wer_ship']} | {s['wer_all']} | {s['rtf_median']} | {s['peak_rss_mb']} |")
    lines += ["", "| Model | Machine | sherpa-onnx | Threads |", "|---|---|---|---|"]
    lines += [f"| {s['model']} | {s['machine']} | {s['sherpa_onnx']} | {s['threads']} |" for s in summary]
    lines += ["", "The real-time factor and the memory of a ground run are from the ground machine, not from the payload computer. "
              "The models with the prefix `em-` ran on the EM.", ""]
    with open(os.path.join(args.results, "RESULTS.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    print("\n".join(lines))


if __name__ == "__main__":
    main()
