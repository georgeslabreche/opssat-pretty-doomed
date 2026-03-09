#!/usr/bin/env python3
"""
Visualize WAV audio files from SDR captures.

Produces:
  - audio_spectrogram.svg   Time-frequency view of demodulated audio
  - audio_waveform.svg      Amplitude over time

Usage:
  python plot_audio.py <file.wav>
  python plot_audio.py <file.wav> --output-dir /output
"""
import argparse
import os
import sys

from output_dir import resolve_output_dir
from iq import read_wav_mono
from plots import plot_audio_spectrogram, plot_audio_waveform


def main():
    parser = argparse.ArgumentParser(description="Visualize WAV audio files")
    parser.add_argument("input", help="Path to .wav file")
    parser.add_argument("--output-dir", help="Output directory (default: same as input file)")
    args = parser.parse_args()

    if not os.path.isfile(args.input):
        print(f"Error: {args.input} not found", file=sys.stderr)
        return 1

    base_dir = args.output_dir or os.path.dirname(args.input) or "."
    output_dir = resolve_output_dir(base_dir, args.input)

    print(f"Reading {args.input}...")
    samples, sample_rate = read_wav_mono(args.input)
    duration = len(samples) / sample_rate
    print(f"  {len(samples)} samples, {duration:.2f}s at {sample_rate} Hz")

    print("Generating plots:")
    save = lambda name: os.path.join(output_dir, name)
    plot_audio_spectrogram(samples, sample_rate, save_path=save("audio_spectrogram.svg"))
    print(f"  {save('audio_spectrogram.svg')}")
    plot_audio_waveform(samples, sample_rate, save_path=save("audio_waveform.svg"))
    print(f"  {save('audio_waveform.svg')}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
