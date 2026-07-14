#!/usr/bin/env python3
"""
Build an SDR-emulator replay file from raw flight captures.

The iio-emu based test (pretty-doomed/docker-compose.emu-test.yml) replays a
cs16 file as if it were the AD9361 stream. Raw flight recordings cannot be used
as-is: the Run 5 captures were recorded at a 1295.5 MHz center (uplink at about
+500 kHz, which the operational chain's channel low-pass around DC would
discard) and at 2.5 MSPS (the emulator-facing config assumes sdr_rate, and the
emulator does not honor rate configuration). This tool converts them:

  1. locate the uplink in each capture (strongest carrier, as measured by the
     same analysis detect_carrier.py uses),
  2. frequency-shift it to a chosen offset near DC (default -8 kHz, the offset
     actually measured at zenith, so a peak search is genuinely exercised
     rather than handed a signal exactly at DC),
  3. resample to the configured radio rate (default 2.4 MSPS),
  4. rescale to the 12-bit range the capture chain expects from the AD9361,
  5. concatenate everything into one replay file.

Produces:
  - <output>            The emulator-ready cs16 (interleaved little-endian int16)
  - a per-input report on stdout (found offset, SNR, placement)

Emulator note: the iio-emu build used by pretty-doomed's emu test consumes two
complex samples per delivered sample (determined empirically: frequencies came
out doubled otherwise), so generate the replay with --out-rate at twice the
configured sdr_rate, e.g. --out-rate 4800000 for sdr_rate=2400000.

Usage:
  python make_emu_replay.py <in1.cs16> [in2.cs16 ...] \
      --output <replay.cs16> [--sample-rate 2500000] [--center-freq 1295.5e6] \
      [--target-freq 1296.0e6] [--out-rate 2400000] [--offset-hz -8000] \
      [--peak 2000]
"""
import argparse
import os
import sys
from math import gcd

import numpy as np
from scipy import signal

from iq import read_sc16, compute_psd, analyze_carrier, mix_baseband


def main():
    p = argparse.ArgumentParser(description="Build an SDR-emulator replay file from raw flight captures")
    p.add_argument("inputs", nargs="+", help="Raw .sc16/.cs16 recordings, concatenated in the order given")
    p.add_argument("--output", required=True, help="Output replay file (.cs16)")
    p.add_argument("--sample-rate", type=int, default=2500000, help="Input sample rate in Hz (default: 2500000)")
    p.add_argument("--center-freq", type=float, default=1295.5e6, help="Input SDR center in Hz (default: 1295.5e6)")
    p.add_argument("--target-freq", type=float, default=1296.0e6, help="Expected uplink in Hz, to locate the carrier (default: 1296.0e6)")
    p.add_argument("--out-rate", type=int, default=2400000, help="Replay sample rate in Hz, matching sdr_rate (default: 2400000)")
    p.add_argument("--offset-hz", type=float, default=-8000.0, help="Where to place the uplink relative to DC in the replay (default: -8000)")
    p.add_argument("--peak", type=float, default=2000.0, help="Peak int16 amplitude of the replay, within the AD9361 12-bit range (default: 2000)")
    args = p.parse_args()

    up, down = args.out_rate // gcd(args.out_rate, args.sample_rate), args.sample_rate // gcd(args.out_rate, args.sample_rate)
    print(f"Resample {args.sample_rate} -> {args.out_rate} Hz ({up}/{down}); uplink placed at {args.offset_hz/1e3:+.1f} kHz")

    pieces = []
    for path in args.inputs:
        if not os.path.isfile(path):
            print(f"Error: {path} not found", file=sys.stderr)
            return 1
        iq = read_sc16(path)
        freqs, psd_db = compute_psd(iq, args.sample_rate, fft_size=8192)
        r = analyze_carrier(iq, args.sample_rate, freqs, psd_db,
                            center_freq=args.center_freq, target_freq=args.target_freq)
        found = r["carrier_offset_hz"]
        # Shift so the found carrier lands at offset_hz from DC.
        shifted = mix_baseband(iq, args.sample_rate, found - args.offset_hz)
        resampled = signal.resample_poly(shifted, up, down)
        pieces.append(resampled)
        print(f"  {os.path.basename(path)}: carrier at {found/1e3:+.1f} kHz "
              f"(SNR {r['snr_db']:.1f} dB) -> {args.offset_hz/1e3:+.1f} kHz, "
              f"{len(resampled)/args.out_rate:.2f}s")

    out = np.concatenate(pieces)
    out *= args.peak / (np.max(np.abs(np.concatenate([out.real, out.imag]))) + 1e-12)
    interleaved = np.empty(2 * len(out), dtype="<i2")
    interleaved[0::2] = np.round(out.real).astype("<i2")
    interleaved[1::2] = np.round(out.imag).astype("<i2")
    interleaved.tofile(args.output)
    print(f"wrote {args.output}: {len(out)/args.out_rate:.2f}s at {args.out_rate} Hz, "
          f"{os.path.getsize(args.output)/1e6:.1f} MB, peak {int(np.max(np.abs(interleaved)))}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
