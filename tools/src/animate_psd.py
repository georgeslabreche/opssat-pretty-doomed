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
import glob
import os
import subprocess
import sys
import tempfile

from output_dir import resolve_output_dir

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.animation as animation
from matplotlib.patches import Rectangle
from matplotlib.ticker import FuncFormatter
import numpy as np


def read_sc16(path):
    """Read sc16 file, return complex64."""
    raw = np.fromfile(path, dtype=np.int16)
    if len(raw) % 2 != 0:
        raw = raw[:-1]
    iq = raw[0::2].astype(np.float32) + 1j * raw[1::2].astype(np.float32)
    return iq / 32768.0


def find_wav_sibling(sc16_path):
    """Find a WAV file in the same directory as the sc16 file."""
    parent = os.path.dirname(sc16_path) or "."
    wavs = sorted(glob.glob(os.path.join(parent, "*.wav")))
    return wavs[0] if wavs else None


def mux_audio(video_path, audio_path, output_path):
    """Mux audio into video with ffmpeg. Returns True on success."""
    cmd = [
        "ffmpeg", "-y",
        "-i", video_path,
        "-i", audio_path,
        "-c:v", "copy",
        "-c:a", "aac", "-b:a", "128k",
        "-shortest",
        "-movflags", "+faststart",
        output_path,
    ]
    result = subprocess.run(cmd, capture_output=True, text=True)
    return result.returncode == 0


def render_animation(iq, sample_rate, save_path, fft_size=4096):
    """Render silent MP4 with spectrogram reveal + animated PSD. Returns num_segments."""
    target_frames = 500
    segment_samples = max(fft_size, len(iq) // target_frames)
    num_segments = len(iq) // segment_samples

    if num_segments < 2:
        return 0

    duration = len(iq) / sample_rate
    fps = num_segments / duration

    # Precompute PSD frames
    window = np.hanning(fft_size)
    freqs = np.linspace(-sample_rate / 2, sample_rate / 2, fft_size)
    freqs_khz = freqs / 1e3

    print("Precomputing PSD frames...")
    psd_frames = []
    for i in range(num_segments):
        seg = iq[i * segment_samples : (i + 1) * segment_samples]
        n_sub = max(1, len(seg) // fft_size)
        truncated = seg[: n_sub * fft_size].reshape(n_sub, fft_size)
        spectra = np.fft.fftshift(np.fft.fft(truncated * window, axis=1), axes=1)
        psd_db = 10 * np.log10(np.mean(np.abs(spectra) ** 2, axis=0) + 1e-20)
        psd_frames.append(psd_db)

    psd_all = np.array(psd_frames)
    psd_min = np.min(psd_all) - 3
    psd_max = np.max(psd_all) + 3

    # Build figure
    print("Computing spectrogram...")
    fig, (ax_spec, ax_psd) = plt.subplots(2, 1, figsize=(10, 7),
                                           gridspec_kw={"height_ratios": [1, 1.2]})

    # Spectrogram with Fs=sample_rate so time axis is in seconds
    Pxx, _, _, im = ax_spec.specgram(iq, NFFT=1024, Fs=sample_rate, noverlap=512,
                                     cmap="viridis", scale="dB", Fc=0)
    spec_db = 10 * np.log10(Pxx + 1e-20)
    im.set_clim(vmin=np.percentile(spec_db, 2), vmax=np.percentile(spec_db, 99.5))
    # Relabel frequency axis to kHz
    ax_spec.yaxis.set_major_formatter(FuncFormatter(lambda x, _: f"{x / 1e3:.0f}"))
    ax_spec.set_ylabel("Frequency (kHz)")
    ax_spec.set_title("PSD Evolution")
    ax_spec.set_xlim(0, duration)

    # White overlay covering the "future" — coordinates in seconds
    ylim = ax_spec.get_ylim()
    overlay = ax_spec.add_patch(
        Rectangle((0, ylim[0]), duration, ylim[1] - ylim[0],
                  facecolor="white", alpha=0.75, zorder=5))
    cursor = ax_spec.axvline(0, color="red", linewidth=2, alpha=0.9, zorder=6)

    # PSD plot
    line, = ax_psd.plot(freqs_khz, psd_frames[0], linewidth=0.8, color="steelblue")
    ax_psd.set_xlim(freqs_khz[0], freqs_khz[-1])
    ax_psd.set_ylim(psd_min, psd_max)
    ax_psd.set_xlabel("Frequency (kHz)")
    ax_psd.set_ylabel("Power (dBFS)")
    ax_psd.grid(True, alpha=0.3)
    time_text = ax_psd.text(0.02, 0.95, "", transform=ax_psd.transAxes,
                            fontsize=10, verticalalignment="top",
                            bbox=dict(boxstyle="round,pad=0.3", facecolor="white", alpha=0.8))
    fig.tight_layout()

    def update(frame):
        t_sec = (frame + 0.5) * segment_samples / sample_rate
        overlay.set_x(t_sec)
        overlay.set_width(max(0, duration - t_sec))
        cursor.set_xdata([t_sec, t_sec])
        line.set_ydata(psd_frames[frame])
        time_text.set_text(f"t = {t_sec:.3f} s")

    print(f"Rendering {num_segments} frames at {fps:.1f} fps (realtime)...")
    anim = animation.FuncAnimation(fig, update, frames=num_segments,
                                   interval=1000 / fps, blit=False)
    writer = animation.FFMpegWriter(fps=fps, bitrate=2000,
                                    extra_args=["-pix_fmt", "yuv420p"])
    anim.save(save_path, writer=writer)
    plt.close(fig)
    return num_segments


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

    if audio_path:
        tmp_fd, tmp_video = tempfile.mkstemp(suffix=".mp4")
        os.close(tmp_fd)
        try:
            num_segments = render_animation(iq, args.sample_rate, tmp_video, args.fft_size)
            if num_segments < 2:
                print("Error: file too short for animation", file=sys.stderr)
                return 1

            print(f"Muxing audio from {audio_path}...")
            if mux_audio(tmp_video, audio_path, output_path):
                print(f"Output: {output_path} ({duration:.1f}s realtime, with audio)")
            else:
                os.rename(tmp_video, output_path)
                tmp_video = None
                print(f"Warning: audio mux failed, output is silent")
                print(f"Output: {output_path} ({duration:.1f}s realtime)")
        finally:
            if tmp_video and os.path.exists(tmp_video):
                os.unlink(tmp_video)
    else:
        num_segments = render_animation(iq, args.sample_rate, output_path, args.fft_size)
        if num_segments < 2:
            print("Error: file too short for animation", file=sys.stderr)
            return 1
        print(f"Output: {output_path} ({duration:.1f}s realtime, silent)")

    return 0


if __name__ == "__main__":
    sys.exit(main())
