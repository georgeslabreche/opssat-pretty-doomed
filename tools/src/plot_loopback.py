#!/usr/bin/env python3
"""
Compare loopback input and output WAV files.

Produces:
  - loopback_overlay.svg       Aligned input vs output waveforms
  - loopback_correlation.svg   Cross-correlation vs lag

Usage:
  python plot_loopback.py <input.wav> <output.wav>
  python plot_loopback.py <input.wav> <output.wav> --output-dir /output
"""
import argparse
import os
import sys

from output_dir import resolve_output_dir

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from scipy.io import wavfile
from scipy.signal import correlate, resample


def read_wav_mono(path):
    """Read WAV, return (samples_float32, sample_rate)."""
    sr, data = wavfile.read(path)
    if data.dtype == np.int16:
        samples = data.astype(np.float32) / 32768.0
    elif data.dtype == np.int32:
        samples = data.astype(np.float32) / 2147483648.0
    else:
        samples = data.astype(np.float32)
    if samples.ndim > 1:
        samples = samples[:, 0]
    return samples, sr


def plot_overlay(input_samples, input_sr, output_samples, output_sr, output_path):
    """Aligned waveform overlay of input and output."""
    # Resample output to input rate if they differ
    if input_sr != output_sr:
        num_out = int(len(output_samples) * input_sr / output_sr)
        output_samples = resample(output_samples, num_out)

    # Use shorter length
    n = min(len(input_samples), len(output_samples))
    t = np.arange(n) / input_sr

    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(14, 6), sharex=True)

    ax1.plot(t, input_samples[:n], linewidth=0.3, color="steelblue", label="Input (TX)")
    ax1.set_ylabel("Amplitude")
    ax1.set_title("Loopback: Input vs Output")
    ax1.legend(loc="upper right")
    ax1.grid(True, alpha=0.3)

    ax2.plot(t, output_samples[:n], linewidth=0.3, color="coral", label="Output (RX)")
    ax2.set_ylabel("Amplitude")
    ax2.set_xlabel("Time (s)")
    ax2.legend(loc="upper right")
    ax2.grid(True, alpha=0.3)

    fig.tight_layout()
    fig.savefig(output_path, format="svg")
    plt.close(fig)
    print(f"  {output_path}")


def plot_correlation(input_samples, input_sr, output_samples, output_sr, output_path, max_lag_ms=100):
    """Normalized cross-correlation vs lag."""
    # Resample output to input rate if they differ
    if input_sr != output_sr:
        num_out = int(len(output_samples) * input_sr / output_sr)
        output_samples = resample(output_samples, num_out)

    sr = input_sr
    n = min(len(input_samples), len(output_samples))
    a = input_samples[:n]
    b = output_samples[:n]

    # Normalize
    a = (a - np.mean(a)) / (np.std(a) + 1e-10)
    b = (b - np.mean(b)) / (np.std(b) + 1e-10)

    max_lag_samples = int(max_lag_ms * sr / 1000)

    # Compute cross-correlation around zero lag
    corr = correlate(b, a, mode="full")
    corr /= n  # normalize
    center = len(a) - 1
    start = max(0, center - max_lag_samples)
    end = min(len(corr), center + max_lag_samples + 1)
    corr_window = corr[start:end]

    lags_samples = np.arange(start - center, end - center)
    lags_ms = lags_samples / sr * 1000

    peak_idx = np.argmax(corr_window)
    peak_lag_ms = lags_ms[peak_idx]
    peak_val = corr_window[peak_idx]

    fig, ax = plt.subplots(figsize=(12, 5))
    ax.plot(lags_ms, corr_window, linewidth=0.8, color="steelblue")
    ax.axvline(peak_lag_ms, color="red", linestyle="--", alpha=0.7,
               label=f"Peak: {peak_val:.3f} at {peak_lag_ms:.1f} ms")
    ax.set_xlabel("Lag (ms)")
    ax.set_ylabel("Normalized Cross-Correlation")
    ax.set_title("Loopback Cross-Correlation")
    ax.legend()
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    fig.savefig(output_path, format="svg")
    plt.close(fig)
    print(f"  {output_path}")
    print(f"  Peak correlation: {peak_val:.3f} at lag {peak_lag_ms:.1f} ms")


def main():
    parser = argparse.ArgumentParser(description="Compare loopback input and output WAV files")
    parser.add_argument("input", help="Path to input (TX) .wav file")
    parser.add_argument("output", help="Path to output (RX) .wav file")
    parser.add_argument("--output-dir", help="Output directory (default: same as output file)")
    args = parser.parse_args()

    for f in [args.input, args.output]:
        if not os.path.isfile(f):
            print(f"Error: {f} not found", file=sys.stderr)
            return 1

    base_dir = args.output_dir or os.path.dirname(args.output) or "."
    output_dir = resolve_output_dir(base_dir, args.output)

    print(f"Reading input:  {args.input}")
    input_samples, input_sr = read_wav_mono(args.input)
    print(f"  {len(input_samples)} samples, {len(input_samples)/input_sr:.2f}s at {input_sr} Hz")

    print(f"Reading output: {args.output}")
    output_samples, output_sr = read_wav_mono(args.output)
    print(f"  {len(output_samples)} samples, {len(output_samples)/output_sr:.2f}s at {output_sr} Hz")

    print("Generating plots:")
    plot_overlay(input_samples, input_sr, output_samples, output_sr,
                 os.path.join(output_dir, "loopback_overlay.svg"))
    plot_correlation(input_samples, input_sr, output_samples, output_sr,
                     os.path.join(output_dir, "loopback_correlation.svg"))

    return 0


if __name__ == "__main__":
    sys.exit(main())
