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

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np


def read_sc16(path):
    """Read sc16 file: interleaved int16 (I, Q, I, Q, ...), return complex64."""
    raw = np.fromfile(path, dtype=np.int16)
    if len(raw) % 2 != 0:
        raw = raw[:-1]
    iq = raw[0::2].astype(np.float32) + 1j * raw[1::2].astype(np.float32)
    return iq / 32768.0  # normalize to [-1, 1]


def compute_psd(iq, sample_rate, fft_size=4096):
    """Compute averaged PSD. Returns (freqs_hz, psd_db)."""
    num_segments = max(1, len(iq) // fft_size)
    truncated = iq[: num_segments * fft_size]
    segments = truncated.reshape(num_segments, fft_size)

    window = np.hanning(fft_size)
    spectra = np.fft.fftshift(
        np.fft.fft(segments * window, axis=1), axes=1
    )
    psd_db = 10 * np.log10(np.mean(np.abs(spectra) ** 2, axis=0) + 1e-20)
    freqs = np.linspace(-sample_rate / 2, sample_rate / 2, fft_size)
    return freqs, psd_db


def compute_signal_stats(iq, sample_rate, freqs, psd_db):
    """Compute signal statistics. Returns dict."""
    i_data = iq.real
    q_data = iq.imag

    # Basic stats
    rms = np.sqrt(np.mean(np.abs(iq) ** 2))
    peak = np.max(np.abs(iq))
    crest_factor_db = 20 * np.log10(peak / rms + 1e-20)

    # DC offset
    dc_i = np.mean(i_data)
    dc_q = np.mean(q_data)
    dc_magnitude = np.sqrt(dc_i ** 2 + dc_q ** 2)

    # I/Q imbalance
    gain_i = np.std(i_data)
    gain_q = np.std(q_data)
    gain_imbalance_db = 20 * np.log10(gain_i / (gain_q + 1e-20))
    # Phase imbalance: correlation between I and Q (ideal = 0 for uncorrelated)
    i_centered = i_data - dc_i
    q_centered = q_data - dc_q
    correlation = np.mean(i_centered * q_centered) / (np.std(i_centered) * np.std(q_centered) + 1e-20)
    phase_imbalance_deg = np.degrees(np.arcsin(np.clip(correlation, -1, 1)))

    # Frequency offset: peak of PSD
    peak_bin = np.argmax(psd_db)
    freq_offset_hz = freqs[peak_bin]

    # SNR estimate: peak power vs median power (noise floor)
    noise_floor_db = np.median(psd_db)
    peak_power_db = psd_db[peak_bin]
    snr_db = peak_power_db - noise_floor_db

    # Occupied bandwidth: -10 dB from peak
    threshold = peak_power_db - 10
    occupied_bins = psd_db >= threshold
    if np.any(occupied_bins):
        occupied_freqs = freqs[occupied_bins]
        occupied_bw = occupied_freqs[-1] - occupied_freqs[0]
    else:
        occupied_bw = 0.0

    return {
        "rms": float(rms),
        "rms_dbfs": float(20 * np.log10(rms + 1e-20)),
        "peak": float(peak),
        "peak_dbfs": float(20 * np.log10(peak + 1e-20)),
        "crest_factor_db": float(crest_factor_db),
        "dc_offset_i": float(dc_i),
        "dc_offset_q": float(dc_q),
        "dc_magnitude": float(dc_magnitude),
        "gain_imbalance_db": float(gain_imbalance_db),
        "phase_imbalance_deg": float(phase_imbalance_deg),
        "freq_offset_hz": float(freq_offset_hz),
        "snr_estimate_db": float(snr_db),
        "noise_floor_db": float(noise_floor_db),
        "occupied_bw_hz": float(occupied_bw),
        "duration_s": float(len(iq) / sample_rate),
        "num_samples": int(len(iq)),
        "sample_rate_hz": int(sample_rate),
    }


def format_stats(stats):
    """Format stats dict as human-readable text."""
    lines = [
        "I/Q Signal Statistics",
        "=" * 40,
        f"Duration:          {stats['duration_s']:.2f} s",
        f"Samples:           {stats['num_samples']:,}",
        f"Sample rate:       {stats['sample_rate_hz']:,} Hz",
        "",
        "Signal Level",
        "-" * 40,
        f"RMS:               {stats['rms']:.4f} ({stats['rms_dbfs']:.1f} dBFS)",
        f"Peak:              {stats['peak']:.4f} ({stats['peak_dbfs']:.1f} dBFS)",
        f"Crest factor:      {stats['crest_factor_db']:.1f} dB",
        "",
        "DC Offset",
        "-" * 40,
        f"I:                 {stats['dc_offset_i']:.6f}",
        f"Q:                 {stats['dc_offset_q']:.6f}",
        f"Magnitude:         {stats['dc_magnitude']:.6f}",
        "",
        "I/Q Imbalance",
        "-" * 40,
        f"Gain imbalance:    {stats['gain_imbalance_db']:.2f} dB",
        f"Phase imbalance:   {stats['phase_imbalance_deg']:.2f} deg",
        "",
        "Spectral",
        "-" * 40,
        f"Freq offset:       {stats['freq_offset_hz']:.0f} Hz",
        f"Noise floor:       {stats['noise_floor_db']:.1f} dB",
        f"SNR estimate:      {stats['snr_estimate_db']:.1f} dB",
        f"Occupied BW:       {stats['occupied_bw_hz']:.0f} Hz (-10 dB from peak)",
    ]
    return "\n".join(lines)


def plot_spectrogram(iq, sample_rate, output_path, fft_size=1024, overlap=512):
    """High-res spectrogram waterfall."""
    fig, ax = plt.subplots(figsize=(12, 6))
    ax.specgram(
        iq, NFFT=fft_size, Fs=sample_rate / 1e3, noverlap=overlap,
        cmap="viridis", scale="dB", vmin=-80, vmax=0,
        Fc=0,
    )
    ax.set_xlabel("Time (s)")
    ax.set_ylabel("Frequency (kHz)")
    ax.set_title("I/Q Spectrogram")
    fig.colorbar(ax.images[0], ax=ax, label="Power (dB)")
    fig.tight_layout()
    fig.savefig(output_path, format="svg")
    plt.close(fig)
    print(f"  {output_path}")


def plot_psd(freqs, psd_db, output_path, stats=None):
    """Plot averaged power spectral density with annotations."""
    fig, ax = plt.subplots(figsize=(12, 5))
    ax.plot(freqs / 1e3, psd_db, linewidth=0.5)

    if stats:
        # Noise floor line
        ax.axhline(stats["noise_floor_db"], color="red", linestyle="--", alpha=0.5,
                    label=f"Noise floor: {stats['noise_floor_db']:.1f} dB")
        # Peak frequency marker
        ax.axvline(stats["freq_offset_hz"] / 1e3, color="green", linestyle="--", alpha=0.5,
                    label=f"Peak: {stats['freq_offset_hz']:.0f} Hz")
        # Occupied BW shading
        threshold = np.max(psd_db) - 10
        mask = psd_db >= threshold
        if np.any(mask):
            ax.fill_between(freqs / 1e3, ax.get_ylim()[0], psd_db,
                            where=mask, alpha=0.15, color="green",
                            label=f"Occupied BW: {stats['occupied_bw_hz']:.0f} Hz")
        ax.legend(fontsize=8)

    ax.set_xlabel("Frequency (kHz)")
    ax.set_ylabel("Power (dBFS)")
    ax.set_title("Power Spectral Density")
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    fig.savefig(output_path, format="svg")
    plt.close(fig)
    print(f"  {output_path}")


def plot_constellation(iq, output_path, max_points=10000):
    """I/Q scatter plot."""
    if len(iq) > max_points:
        indices = np.random.default_rng(42).choice(len(iq), max_points, replace=False)
        iq_sub = iq[indices]
    else:
        iq_sub = iq

    fig, ax = plt.subplots(figsize=(7, 7))
    ax.scatter(iq_sub.real, iq_sub.imag, s=4, alpha=0.5, c="steelblue", edgecolors="none")
    ax.set_xlabel("I")
    ax.set_ylabel("Q")
    ax.set_title("I/Q Constellation")
    ax.set_aspect("equal")
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    fig.savefig(output_path, format="svg")
    plt.close(fig)
    print(f"  {output_path}")


def plot_waveform(iq, sample_rate, output_path, max_seconds=0.01):
    """Time-domain I and Q waveform (first N seconds)."""
    max_samples = int(max_seconds * sample_rate)
    iq_sub = iq[:max_samples]
    t = np.arange(len(iq_sub)) / sample_rate * 1e3  # ms

    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(12, 5), sharex=True)
    ax1.plot(t, iq_sub.real, linewidth=0.5, color="steelblue")
    ax1.set_ylabel("I")
    ax1.set_title(f"I/Q Waveform (first {max_seconds*1e3:.1f} ms)")
    ax1.grid(True, alpha=0.3)

    ax2.plot(t, iq_sub.imag, linewidth=0.5, color="coral")
    ax2.set_ylabel("Q")
    ax2.set_xlabel("Time (ms)")
    ax2.grid(True, alpha=0.3)

    fig.tight_layout()
    fig.savefig(output_path, format="svg")
    plt.close(fig)
    print(f"  {output_path}")


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
    plot_spectrogram(iq, args.sample_rate, os.path.join(output_dir, "spectrogram.svg"), fft_size=args.fft_size)
    plot_psd(freqs, psd_db, os.path.join(output_dir, "psd.svg"), stats=stats)
    plot_constellation(iq, os.path.join(output_dir, "constellation.svg"))
    plot_waveform(iq, args.sample_rate, os.path.join(output_dir, "waveform.svg"))

    return 0


if __name__ == "__main__":
    sys.exit(main())
