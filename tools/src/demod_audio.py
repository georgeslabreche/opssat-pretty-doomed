#!/usr/bin/env python3
"""
Demodulate a raw sc16/cs16 I/Q file to audio (WAV).

The on-board pipeline demodulates during capture; raw I/Q downlinks arrive
without audio. This reproduces that step on the ground so recordings can be
listened to.

Modes:
  fm  (default)  Mirrors the flight chain: mix the carrier to baseband, channel
                 low-pass, quadrature FM demod, resample, 300-3400 Hz voice
                 band-pass. Use for the voice broadcasts.
  cw             Mix the carrier to an audible beat tone (BFO). Use to hear a
                 keyed / CW transmission as on/off tones.
  ssb            Single-sideband product detector (--sideband auto/lsb/usb).
                 Use for amateur SSB voice: tune to the suppressed carrier and
                 recover one sideband. Set --offset/--auto to the carrier.

The carrier can be given as an absolute frequency (--carrier-freq with
--center-freq), a baseband offset (--offset), or found automatically (--auto).

Produces:
  - audio.wav    Demodulated mono audio (16 kHz by default)

Usage:
  python demod_audio.py <file.cs16> --sample-rate 2500000 --mode cw --auto \
      --center-freq 1295.5e6
  python demod_audio.py <file.sc16> --sample-rate 200000 --mode fm --offset -8000
"""
import argparse
import os
import sys

import numpy as np

from output_dir import resolve_output_dir
from iq import (read_sc16, compute_psd, analyze_carrier, mix_baseband, channelize,
                fm_demodulate, ssb_demodulate, resample_to, bandpass, write_wav_mono)


def resolve_offset(args, iq):
    """Determine the baseband offset (Hz) of the carrier to demodulate."""
    if args.auto:
        freqs, psd_db = compute_psd(iq, args.sample_rate, fft_size=8192)
        r = analyze_carrier(iq, args.sample_rate, freqs, psd_db)
        return r["carrier_offset_hz"]
    if args.offset is not None:
        return args.offset
    if args.carrier_freq is not None and args.center_freq is not None:
        return args.carrier_freq - args.center_freq
    return 0.0


def main():
    p = argparse.ArgumentParser(description="Demodulate sc16/cs16 I/Q to audio (WAV)")
    p.add_argument("input", help="Path to .sc16 / .cs16 file")
    p.add_argument("--sample-rate", type=int, required=True, help="Input sample rate in Hz")
    p.add_argument("--mode", choices=["fm", "cw", "ssb"], default="fm", help="Demodulation mode (default: fm)")
    p.add_argument("--sideband", choices=["auto", "lsb", "usb"], default="auto",
                   help="Sideband for --mode ssb (default: auto, pick the stronger)")
    p.add_argument("--auto", action="store_true", help="Auto-detect the strongest carrier and tune to it")
    p.add_argument("--offset", type=float, default=None, help="Carrier baseband offset in Hz")
    p.add_argument("--carrier-freq", type=float, default=None, help="Carrier absolute frequency in Hz (with --center-freq)")
    p.add_argument("--center-freq", type=float, default=None, help="SDR center frequency in Hz")
    p.add_argument("--deviation", type=float, default=5000.0, help="FM deviation in Hz (default: 5000, flight NBFM)")
    p.add_argument("--channel-bw", type=float, default=15000.0, help="Channel bandwidth in Hz before demod (default: 15000)")
    p.add_argument("--audio-rate", type=int, default=16000, help="Output audio sample rate in Hz (default: 16000)")
    p.add_argument("--bandpass", default="300,3400", help="Voice band-pass 'low,high' Hz, or 'none' (default: 300,3400)")
    p.add_argument("--bfo", type=float, default=800.0, help="CW beat tone in Hz for --mode cw (default: 800)")
    p.add_argument("--output-dir", help="Output directory (default: same as input file)")
    args = p.parse_args()

    if not os.path.isfile(args.input):
        print(f"Error: {args.input} not found", file=sys.stderr)
        return 1

    base_dir = args.output_dir or os.path.dirname(args.input) or "."
    output_dir = resolve_output_dir(base_dir, args.input)

    print(f"Reading {args.input}...")
    iq = read_sc16(args.input)
    print(f"  {len(iq)} samples, {len(iq)/args.sample_rate:.2f}s at {args.sample_rate} Hz")

    offset = resolve_offset(args, iq)
    print(f"Tuning to carrier offset {offset/1e3:+.1f} kHz "
          f"({args.mode.upper()} mode)")

    # Mix the carrier to baseband and channel-filter with decimation.
    bb = mix_baseband(iq, args.sample_rate, offset)
    decim = max(1, int(args.sample_rate // max(args.channel_bw * 2, args.audio_rate)))
    bb, mid_rate = channelize(bb, args.sample_rate, args.channel_bw, decim=decim)
    print(f"  channelized to {mid_rate:.0f} Hz (decim {decim})")

    if args.mode == "fm":
        audio = fm_demodulate(bb, mid_rate, args.deviation)
    elif args.mode == "ssb":  # single-sideband product detector, for voice
        lo, hi = (float(v) for v in args.bandpass.split(",")) if args.bandpass.lower() != "none" else (300.0, 3400.0)
        audio, used = ssb_demodulate(bb, mid_rate, sideband=args.sideband, voice_band=(lo, hi))
        print(f"  SSB {used.upper()} sideband")
    else:  # cw: shift to an audible beat tone and take the real part
        t = np.arange(len(bb)) / mid_rate
        audio = (bb * np.exp(1j * 2 * np.pi * args.bfo * t)).real

    audio = resample_to(audio, mid_rate, args.audio_rate)

    if args.bandpass.lower() != "none":
        lo, hi = (float(v) for v in args.bandpass.split(","))
        audio = bandpass(audio, args.audio_rate, lo, hi)

    out = os.path.join(output_dir, "audio.wav")
    write_wav_mono(out, audio, args.audio_rate)
    print(f"  wrote {out} ({len(audio)/args.audio_rate:.2f}s at {args.audio_rate} Hz)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
