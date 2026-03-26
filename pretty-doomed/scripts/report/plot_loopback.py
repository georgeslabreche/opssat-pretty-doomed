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
from iq import read_wav_mono
from plots import plot_loopback


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
    save = lambda name: os.path.join(output_dir, name)
    _, _, peak_val, peak_lag = plot_loopback(
        input_samples, input_sr, output_samples, output_sr,
        overlay_save_path=save("loopback_overlay.svg"),
        corr_save_path=save("loopback_correlation.svg"))

    print(f"  {save('loopback_overlay.svg')}")
    print(f"  {save('loopback_correlation.svg')}")
    print(f"  Peak correlation: {peak_val:.3f} at lag {peak_lag:.1f} ms")

    return 0


if __name__ == "__main__":
    sys.exit(main())
