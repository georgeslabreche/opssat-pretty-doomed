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
from plots import plot_psd_comparison


def main():
    parser = argparse.ArgumentParser(description="Compare PSD curves from multiple sc16 files")
    parser.add_argument("inputs", nargs="+", help="Paths to .sc16 files")
    parser.add_argument("--sample-rate", type=int, default=200000,
                        help="Sample rate in Hz (default: 200000)")
    parser.add_argument("--output-dir", default=".", help="Output directory (default: current)")
    parser.add_argument("--fft-size", type=int, default=4096, help="FFT size (default: 4096)")
    args = parser.parse_args()

    valid_paths = []
    for path in args.inputs:
        if not os.path.isfile(path):
            print(f"Warning: {path} not found, skipping", file=sys.stderr)
        else:
            valid_paths.append(path)

    if not valid_paths:
        print("Error: no valid input files", file=sys.stderr)
        return 1

    # Derive subfolder from first input's parent run directory
    first_input = valid_paths[0]
    subdir = derive_output_subdir(os.path.dirname(first_input))
    output_dir = os.path.join(args.output_dir, subdir)
    os.makedirs(output_dir, exist_ok=True)

    output_path = os.path.join(output_dir, "psd_comparison.svg")
    print(f"Reading {len(valid_paths)} files...")
    plot_psd_comparison(valid_paths, args.sample_rate, save_path=output_path, fft_size=args.fft_size)
    print(f"Output: {output_path}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
