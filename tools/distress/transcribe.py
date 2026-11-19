#!/usr/bin/env python3
"""Transcribe the distress call clips with each candidate speech-to-text model.

Each model runs in its own process, so that the peak memory of the process is
the memory of that model only. For each model, the script writes:

    pretty-doomed/docs/distress/results/<model>/<clip>.txt   the model output for each clip
    pretty-doomed/docs/distress/results/<model>/run.json     settings, load time, real-time factor per clip, and peak memory

Usage (from tools/distress, after fetch_models.sh and prepare_clips.sh):

    python3 transcribe.py                      # all models in models.tsv
    python3 transcribe.py --models small-int8  # some models, comma-separated
"""

import argparse
import csv
import glob
import json
import os
import platform
import resource
import subprocess
import sys
import time

import paths

import sherpa_onnx
import soundfile

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_WINDOW_S = 10
MIN_WINDOW_S = 0.5


def read_models(path):
    """Read models.tsv. Return {name: row}."""
    with open(path, encoding="utf-8") as f:
        return {row["name"]: row for row in csv.DictReader(f, delimiter="\t")}


def make_recognizer(row, model_dir, threads):
    """Make a sherpa-onnx offline transducer recognizer for one row of models.tsv."""
    d = os.path.join(model_dir, row["archive"])
    kwargs = dict(
        encoder=os.path.join(d, row["encoder"]),
        decoder=os.path.join(d, row["decoder"]),
        joiner=os.path.join(d, row["joiner"]),
        tokens=os.path.join(d, "tokens.txt"),
        num_threads=threads,
        decoding_method=row["decoding"],
    )
    if row["type"] == "nemo":
        kwargs["model_type"] = "nemo_transducer"
    return sherpa_onnx.OfflineRecognizer.from_transducer(**kwargs)


def peak_rss_mb():
    """Return the peak resident memory of this process in MB."""
    rss = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    return rss / 2**20 if sys.platform == "darwin" else rss / 2**10


def run_model(row, audio_dir, model_dir, out_dir, threads, window_s):
    """Transcribe all clips with one model, and write the outputs."""
    os.makedirs(out_dir, exist_ok=True)
    t0 = time.perf_counter()
    recognizer = make_recognizer(row, model_dir, threads)
    load_s = time.perf_counter() - t0
    clips = {}
    for wav in sorted(glob.glob(os.path.join(audio_dir, "*.wav"))):
        clip = os.path.splitext(os.path.basename(wav))[0]
        audio, rate = soundfile.read(wav, dtype="float32")
        parts = []
        t = time.perf_counter()
        step = int(window_s * rate)
        for start in range(0, len(audio), step):
            window = audio[start:start + step]
            if len(window) < MIN_WINDOW_S * rate:
                continue
            stream = recognizer.create_stream()
            stream.accept_waveform(rate, window)
            recognizer.decode_stream(stream)
            parts.append(stream.result.text.strip())
        seconds = len(audio) / rate
        clips[clip] = {"audio_s": round(seconds, 2), "rtf": round((time.perf_counter() - t) / seconds, 4)}
        with open(os.path.join(out_dir, clip + ".txt"), "w", encoding="utf-8") as f:
            f.write(" ".join(p for p in parts if p) + "\n")
    files = [os.path.join(model_dir, row["archive"], row[k]) for k in ("encoder", "decoder", "joiner")]
    run = {
        "model": row["name"],
        "archive": row["archive"],
        "decoding": row["decoding"],
        "threads": threads,
        "window_s": window_s,
        "sherpa_onnx": sherpa_onnx.__version__,
        "machine": f"{platform.system()} {platform.machine()}",
        "model_mb": round(sum(os.path.getsize(f) for f in files) / 2**20, 1),
        "load_s": round(load_s, 2),
        "peak_rss_mb": round(peak_rss_mb(), 1),
        "clips": clips,
    }
    with open(os.path.join(out_dir, "run.json"), "w", encoding="utf-8") as f:
        json.dump(run, f, indent=1)
        f.write("\n")
    return run


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--models", help="comma-separated model names (default: all in models.tsv)")
    parser.add_argument("--audio", default=os.path.join(HERE, "audio"), help="WAV directory from prepare_clips.sh")
    parser.add_argument("--model-dir", default=os.path.join(HERE, "models"), help="directory from fetch_models.sh")
    parser.add_argument("--out", default=paths.RESULTS, help="output directory")
    parser.add_argument("--threads", type=int, default=1, help="threads per model (default 1, as on the flight)")
    parser.add_argument("--window", type=float, default=DEFAULT_WINDOW_S, help=f"window length in seconds (default {DEFAULT_WINDOW_S})")
    parser.add_argument("--worker", help=argparse.SUPPRESS)
    args = parser.parse_args()

    models = read_models(os.path.join(HERE, "models.tsv"))
    if args.worker:
        run = run_model(models[args.worker], args.audio, args.model_dir, os.path.join(args.out, args.worker), args.threads, args.window)
        print(f"{run['model']}: load {run['load_s']} s, peak {run['peak_rss_mb']} MB")
        return
    if not glob.glob(os.path.join(args.audio, "*.wav")):
        sys.exit(f"No WAV files in {args.audio}. Run prepare_clips.sh first.")
    names = args.models.split(",") if args.models else list(models)
    for name in names:
        if name not in models:
            sys.exit(f"Unknown model: {name}")
        subprocess.run([sys.executable, os.path.abspath(__file__), "--worker", name, "--audio", args.audio,
                        "--model-dir", args.model_dir, "--out", args.out, "--threads", str(args.threads), "--window", str(args.window)], check=True)


if __name__ == "__main__":
    main()
