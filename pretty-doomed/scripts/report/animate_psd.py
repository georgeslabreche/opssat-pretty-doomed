#!/usr/bin/env python3
"""
Animate PSD evolution over time from an sc16 I/Q file.

Produces a realtime MP4 video with two synchronized panels:
  - Top: spectrogram progressively revealed left-to-right
  - Bottom: PSD curve for the current time segment

If a WAV file exists alongside the sc16 (or is provided via --audio),
the demodulated audio is muxed into the video as a soundtrack.

Video duration matches the capture duration (realtime playback).

Usage:
  python animate_psd.py <file.sc16> --sample-rate 200000
  python animate_psd.py <file.sc16> --sample-rate 200000 --audio demod.wav --output-dir /output
"""
import argparse
import os
import sys

from output_dir import resolve_output_dir
from iq import read_sc16
from animation import find_wav_sibling, render_animation


def main():
    parser = argparse.ArgumentParser(description="Animate PSD evolution from sc16 file")
    parser.add_argument("input", help="Path to .sc16 file")
    parser.add_argument("--sample-rate", type=int, default=200000,
                        help="Sample rate in Hz (default: 200000)")
    parser.add_argument("--output-dir", help="Output directory (default: same as input file)")
    parser.add_argument("--fft-size", type=int, default=4096, help="FFT size (default: 4096)")
    parser.add_argument("--audio", help="Path to WAV file for soundtrack (default: auto-detect sibling)")
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

    # Find audio
    audio_path = args.audio
    if not audio_path:
        audio_path = find_wav_sibling(args.input)
    if audio_path and os.path.isfile(audio_path):
        print(f"  Audio: {audio_path}")
    else:
        audio_path = None
        print("  No audio file found, video will be silent")

    output_path = os.path.join(output_dir, "psd_evolution.mp4")
    print(f"Rendering PSD animation...")
    result = render_animation(iq, args.sample_rate, output_path,
                              audio_path=audio_path, fft_size=args.fft_size)

    if result is None:
        print("Error: file too short for animation", file=sys.stderr)
        return 1

    label = "with audio" if audio_path else "silent"
    print(f"Output: {output_path} ({duration:.1f}s realtime, {label})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
