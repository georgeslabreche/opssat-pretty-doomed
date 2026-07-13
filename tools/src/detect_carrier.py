#!/usr/bin/env python3
"""
Detect and characterize a narrowband carrier in a wideband sc16/cs16 I/Q file.

Built for the RF-link test: the spacecraft records a wide band with the SDR
center deliberately offset from the expected uplink, so a ground carrier lands
away from the DC spike. This tool finds the strongest carrier, measures how far
it stands above the noise floor, its width, and whether it keys on and off or
drifts (Doppler) across the recording.

Produces:
  - spectrogram.svg      Full-band waterfall
  - carrier.svg          Spectrogram with the carrier marked + on/off envelope
  - psd.svg              Averaged power spectral density
  - carrier.txt          Carrier detection summary (human-readable)
  - carrier.json         Carrier detection summary (machine-readable)

Usage:
  python detect_carrier.py <file.cs16> --sample-rate 2500000 \
      --center-freq 1295.5e6 --target-freq 1296.0e6
  python detect_carrier.py <file.sc16> --sample-rate 2500000 --output-dir /output
"""
import argparse
import json
import os
import sys

from output_dir import resolve_output_dir
from iq import read_sc16, compute_psd, analyze_carrier, format_carrier_report
from plots import plot_spectrogram, plot_psd, plot_carrier, plot_carrier_drift


def main():
    parser = argparse.ArgumentParser(description="Detect a carrier in a wideband sc16/cs16 I/Q file")
    parser.add_argument("input", help="Path to .sc16 / .cs16 file")
    parser.add_argument("--sample-rate", type=int, required=True,
                        help="Sample rate in Hz (e.g. 2500000)")
    parser.add_argument("--center-freq", type=float, default=None,
                        help="SDR center frequency in Hz, for absolute labeling (e.g. 1295.5e6)")
    parser.add_argument("--target-freq", type=float, default=None,
                        help="Expected uplink frequency in Hz, to report offset (e.g. 1296.0e6)")
    parser.add_argument("--output-dir", help="Output directory (default: same as input file)")
    parser.add_argument("--fft-size", type=int, default=8192,
                        help="FFT size for PSD and detection (default: 8192)")
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

    freqs, psd_db = compute_psd(iq, args.sample_rate, fft_size=args.fft_size)
    report = analyze_carrier(iq, args.sample_rate, freqs, psd_db,
                             center_freq=args.center_freq, target_freq=args.target_freq)

    report_text = format_carrier_report(report)
    print(f"\n{report_text}\n")

    txt_path = os.path.join(output_dir, "carrier.txt")
    with open(txt_path, "w") as f:
        f.write(report_text + "\n")
    print(f"  {txt_path}")

    json_path = os.path.join(output_dir, "carrier.json")
    with open(json_path, "w") as f:
        json.dump(report, f, indent=2)
    print(f"  {json_path}")

    print("Generating plots:")
    save = lambda name: os.path.join(output_dir, name)
    plot_spectrogram(iq, args.sample_rate, save_path=save("spectrogram.svg"), fft_size=args.fft_size)
    print(f"  {save('spectrogram.svg')}")
    plot_carrier(iq, args.sample_rate, report["carrier_offset_hz"],
                 save_path=save("carrier.svg"), fft_size=args.fft_size,
                 center_freq=args.center_freq)
    print(f"  {save('carrier.svg')}")
    plot_carrier_drift(report["drift"], save_path=save("carrier_drift.svg"))
    print(f"  {save('carrier_drift.svg')}")
    plot_psd(freqs, psd_db, save_path=save("psd.svg"))
    print(f"  {save('psd.svg')}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
