#!/usr/bin/env python3
"""Import an EM distress run into the results format of transcribe.py.

The distress package (./run) on the EM writes toGround/distress-NNNNN/<model>/ with
stt.tsv, timing.tsv, and <clip>/transcription.txt. This script writes
results/em-<model>/<clip>.txt and results/em-<model>/run.json, so that
score.py scores the EM run next to the ground run.

Usage (from tools/distress):

    python3 import_em.py <path to distress-NNNNN> [--label em]
    python3 score.py
"""

import argparse
import csv
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def read_tsv(path):
    with open(path, encoding="utf-8") as f:
        return list(csv.DictReader(f, delimiter="\t"))


def read_manifest(path):
    """Read the key: value lines of manifest.txt."""
    values = {}
    if os.path.exists(path):
        with open(path, encoding="utf-8") as f:
            for line in f:
                key, sep, value = line.partition(":")
                if sep and not line.startswith(" "):
                    values.setdefault(key.strip(), value.strip())
    return values


def model_mb(name):
    """Return the model size in MB from a ground results run.json, or None."""
    run = os.path.join(HERE, "results", name, "run.json")
    if os.path.exists(run):
        with open(run, encoding="utf-8") as f:
            return json.load(f).get("model_mb")
    return None


def base_model(config):
    """The model of a config: the config name without a window (-w5) or thread (-t2) suffix."""
    return re.sub(r"-[wt]\d+$", "", config)


def import_model(run_dir, model, out_dir, manifest, machine=None):
    """Write the results of one model. Return the number of clips, or 0 if the model has no output."""
    model_dir = os.path.join(run_dir, model)
    timing_path = os.path.join(model_dir, "timing.tsv")
    if not os.path.exists(timing_path):
        return 0
    timing = read_tsv(timing_path)
    stt_path = os.path.join(model_dir, "stt.tsv")
    stt = read_tsv(stt_path)[0] if os.path.exists(stt_path) else {}
    os.makedirs(out_dir, exist_ok=True)
    clips = {}
    for row in timing:
        clip = row["clip"]
        with open(os.path.join(model_dir, clip, "transcription.txt"), encoding="utf-8") as f:
            text = f.read().strip()
        with open(os.path.join(out_dir, clip + ".txt"), "w", encoding="utf-8") as f:
            f.write(text + "\n")
        clips[clip] = {"audio_s": float(row["audio_s"]), "rtf": float(row["rtf"])}
    peak_kb = int(stt.get("peak_rss_kb", -1))
    run = {
        "model": os.path.basename(out_dir),
        "archive": model,
        "decoding": stt.get("decoding", ""),
        "threads": int(stt.get("threads", 0) or 0),
        "window_s": float(stt.get("window_s", 0) or 0),
        "sherpa_onnx": "1.13.8 (distress binary)",
        "machine": machine or (f"EM ({manifest['uname'].split()[-2]})" if manifest.get("uname") else "ground, Docker"),
        "binary": manifest.get("version", ""),
        "model_mb": model_mb(base_model(model)),
        "load_s": round(int(stt.get("load_ms", -1)) / 1000, 2),
        "peak_rss_mb": round(peak_kb / 1024, 1) if peak_kb > 0 else None,
        "clips": clips,
    }
    with open(os.path.join(out_dir, "run.json"), "w", encoding="utf-8") as f:
        json.dump(run, f, indent=1)
        f.write("\n")
    return len(clips)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("run_dir", help="the toGround/distress-NNNNN directory of an EM run")
    parser.add_argument("--label", default="em", help="prefix of the results directories (default: em)")
    parser.add_argument("--machine", help="machine name in run.json (default: EM (<arch>) from the manifest)")
    parser.add_argument("--results", default=os.path.join(HERE, "results"), help="results directory")
    args = parser.parse_args()

    manifest = read_manifest(os.path.join(args.run_dir, "manifest.txt"))
    models = manifest.get("models", "").split() or sorted(
        d for d in os.listdir(args.run_dir) if os.path.isdir(os.path.join(args.run_dir, d)))
    imported = 0
    for model in models:
        out_dir = os.path.join(args.results, f"{args.label}-{model}")
        n = import_model(args.run_dir, model, out_dir, manifest, args.machine)
        print(f"{model}: {n} clip(s)" if n else f"{model}: no output")
        imported += n
    if not imported:
        sys.exit("Nothing imported.")


if __name__ == "__main__":
    main()
