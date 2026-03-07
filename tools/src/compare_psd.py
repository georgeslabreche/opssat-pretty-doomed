#!/usr/bin/env python3
"""
Compare PSD curves from multiple sc16 I/Q files on a single plot.

Useful for comparing RF environment across captures or runs.

Produces:
  - psd_comparison.svg    Overlaid PSD curves with legend

Usage:
  python compare_psd.py file1.sc16 file2.sc16 file3.sc16 --sample-rate 200000
  python compare_psd.py /data/run-*/capture.sc16 --sample-rate 200000 --output-dir /output
"""
import argparse
import os
import sys

from output_dir import derive_output_subdir

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np


def read_sc16(path):
    """Read sc16 file, return complex64."""
    raw = np.fromfile(path, dtype=np.int16)
    if len(raw) % 2 != 0:
        raw = raw[:-1]
    iq = raw[0::2].astype(np.float32) + 1j * raw[1::2].astype(np.float32)
    return iq / 32768.0


def compute_psd(iq, sample_rate, fft_size=4096):
    """Compute averaged PSD. Returns (freqs_hz, psd_db)."""
    num_segments = max(1, len(iq) // fft_size)
    truncated = iq[: num_segments * fft_size]
    segments = truncated.reshape(num_segments, fft_size)

    window = np.hanning(fft_size)
    spectra = np.fft.fftshift(
        np.fft.fft(segments * window, axis=1), axes=1
    )
    psd_db = 10 * np.log10(np.mean(np.abs(spectra) ** 2, axis=0) + 1e-20)
    freqs = np.linspace(-sample_rate / 2, sample_rate / 2, fft_size)
    return freqs, psd_db


def make_label(path):
    """Create a short label from a file path."""
    parts = path.replace("\\", "/").split("/")
    # Try to find a distinctive part (run-NNNNNN, capture-NNN, or filename)
    interesting = []
    for p in parts:
        if p.startswith("run-") or p.startswith("capture-") or p.startswith("pack-"):
            interesting.append(p)
    if interesting:
        return "/".join(interesting)
    return os.path.basename(path)


def main():
    parser = argparse.ArgumentParser(description="Compare PSD curves from multiple sc16 files")
    parser.add_argument("inputs", nargs="+", help="Paths to .sc16 files")
    parser.add_argument("--sample-rate", type=int, default=200000,
                        help="Sample rate in Hz (default: 200000)")
    parser.add_argument("--output-dir", default=".", help="Output directory (default: current)")
    parser.add_argument("--fft-size", type=int, default=4096, help="FFT size (default: 4096)")
    args = parser.parse_args()

    # Derive subfolder from first input's parent run directory
    first_input = args.inputs[0] if args.inputs else ""
    subdir = derive_output_subdir(os.path.dirname(first_input))
    output_dir = os.path.join(args.output_dir, subdir)
    os.makedirs(output_dir, exist_ok=True)

    fig, ax = plt.subplots(figsize=(14, 6))

    for path in args.inputs:
        if not os.path.isfile(path):
            print(f"Warning: {path} not found, skipping", file=sys.stderr)
            continue

        print(f"Reading {path}...")
        iq = read_sc16(path)
        freqs, psd_db = compute_psd(iq, args.sample_rate, args.fft_size)

        label = make_label(path)
        ax.plot(freqs / 1e3, psd_db, linewidth=0.6, label=label, alpha=0.8)

    ax.set_xlabel("Frequency (kHz)")
    ax.set_ylabel("Power (dBFS)")
    ax.set_title("PSD Comparison")
    ax.grid(True, alpha=0.3)
    ax.legend(fontsize=7, loc="upper right")
    fig.tight_layout()

    output_path = os.path.join(output_dir, "psd_comparison.svg")
    fig.savefig(output_path, format="svg")
    plt.close(fig)
    print(f"Output: {output_path}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
