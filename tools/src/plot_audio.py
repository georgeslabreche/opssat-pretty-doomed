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

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from scipy.io import wavfile


def plot_audio_spectrogram(samples, sample_rate, output_path, fft_size=512):
    """Time-frequency spectrogram of audio."""
    fig, ax = plt.subplots(figsize=(12, 5))
    ax.specgram(
        samples, NFFT=fft_size, Fs=sample_rate / 1e3, noverlap=fft_size // 2,
        cmap="inferno", scale="dB", vmin=-80, vmax=0,
    )
    ax.set_xlabel("Time (s)")
    ax.set_ylabel("Frequency (kHz)")
    ax.set_title("Audio Spectrogram")
    fig.colorbar(ax.images[0], ax=ax, label="Power (dB)")
    fig.tight_layout()
    fig.savefig(output_path, format="svg")
    plt.close(fig)
    print(f"  {output_path}")


def plot_audio_waveform(samples, sample_rate, output_path):
    """Audio amplitude over time."""
    t = np.arange(len(samples)) / sample_rate

    fig, ax = plt.subplots(figsize=(12, 4))
    ax.plot(t, samples, linewidth=0.3, color="steelblue")
    ax.set_xlabel("Time (s)")
    ax.set_ylabel("Amplitude")
    ax.set_title("Audio Waveform")
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    fig.savefig(output_path, format="svg")
    plt.close(fig)
    print(f"  {output_path}")


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
    sample_rate, data = wavfile.read(args.input)

    # Convert to float, handle multi-channel
    if data.dtype == np.int16:
        samples = data.astype(np.float32) / 32768.0
    elif data.dtype == np.int32:
        samples = data.astype(np.float32) / 2147483648.0
    else:
        samples = data.astype(np.float32)

    if samples.ndim > 1:
        samples = samples[:, 0]  # use first channel

    duration = len(samples) / sample_rate
    print(f"  {len(samples)} samples, {duration:.2f}s at {sample_rate} Hz")

    print("Generating plots:")
    plot_audio_spectrogram(samples, sample_rate, os.path.join(output_dir, "audio_spectrogram.svg"))
    plot_audio_waveform(samples, sample_rate, os.path.join(output_dir, "audio_waveform.svg"))

    return 0


if __name__ == "__main__":
    sys.exit(main())
