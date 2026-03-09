#!/usr/bin/env python3
"""
Visualize sc16 I/Q files from SDR captures.

Produces:
  - spectrogram.svg      High-res waterfall (time x frequency)
  - psd.svg              Averaged power spectral density
  - constellation.svg    I/Q scatter plot
  - waveform.svg         Time-domain I and Q amplitude
  - iq_stats.txt         Signal statistics summary

Usage:
  python plot_iq.py <file.sc16> --sample-rate 200000
  python plot_iq.py <file.sc16> --sample-rate 200000 --output-dir /output
"""
import argparse
import json
import os
import sys

from output_dir import resolve_output_dir
from iq import read_sc16, compute_psd, compute_signal_stats, format_stats
from plots import plot_spectrogram, plot_psd, plot_constellation, plot_waveform


def main():
    parser = argparse.ArgumentParser(description="Visualize sc16 I/Q files")
    parser.add_argument("input", help="Path to .sc16 file")
    parser.add_argument("--sample-rate", type=int, default=200000,
                        help="Sample rate in Hz (default: 200000 = effective rate after decimation)")
    parser.add_argument("--output-dir", help="Output directory (default: same as input file)")
    parser.add_argument("--fft-size", type=int, default=1024, help="FFT size for spectrogram (default: 1024)")
    args = parser.parse_args()

    if not os.path.isfile(args.input):
        print(f"Error: {args.input} not found", file=sys.stderr)
        return 1

    base_dir = args.output_dir or os.path.dirname(args.input) or "."
    output_dir = resolve_output_dir(base_dir, args.input)

    print(f"Reading {args.input}...")
    iq = read_sc16(args.input)
    duration = len(iq) / args.sample_rate
    print(f"  {len(iq)} samples, {duration:.2f}s at {args.sample_rate} Hz")

    # Compute PSD and stats
    freqs, psd_db = compute_psd(iq, args.sample_rate)
    stats = compute_signal_stats(iq, args.sample_rate, freqs, psd_db)

    # Print and save stats
    stats_text = format_stats(stats)
    print(f"\n{stats_text}\n")

    stats_path = os.path.join(output_dir, "iq_stats.txt")
    with open(stats_path, "w") as f:
        f.write(stats_text + "\n")
    print(f"  {stats_path}")

    stats_json_path = os.path.join(output_dir, "iq_stats.json")
    with open(stats_json_path, "w") as f:
        json.dump(stats, f, indent=2)
    print(f"  {stats_json_path}")

    print("Generating plots:")
    save = lambda name: os.path.join(output_dir, name)
    plot_spectrogram(iq, args.sample_rate, save_path=save("spectrogram.svg"), fft_size=args.fft_size)
    print(f"  {save('spectrogram.svg')}")
    plot_psd(freqs, psd_db, save_path=save("psd.svg"), stats=stats)
    print(f"  {save('psd.svg')}")
    plot_constellation(iq, save_path=save("constellation.svg"))
    print(f"  {save('constellation.svg')}")
    plot_waveform(iq, args.sample_rate, save_path=save("waveform.svg"))
    print(f"  {save('waveform.svg')}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
