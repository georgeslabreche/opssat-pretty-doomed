#!/usr/bin/env python3
"""
Generate an HTML report for an SDR capture or loopback run directory.

Processes all sc16 and WAV files, generates plots, and produces a single
HTML page with embedded SVGs for easy review.

Usage:
  python report.py /data/capture-artifacts/pack-4023_.../chg/toGround/run-000001 --output-dir /output
  python report.py /data/loopback-artifacts/run-000001 --output-dir /output --loopback --input-wav /data/samples/input.wav
"""
import argparse
import base64
import glob
import json
import os
import subprocess
import sys
import tempfile
import textwrap
from datetime import datetime
from io import BytesIO

from output_dir import resolve_output_dir

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.animation as animation
import numpy as np
from scipy.io import wavfile


# ---------------------------------------------------------------------------
# I/Q helpers (duplicated from plot_iq.py to keep report.py self-contained)
# ---------------------------------------------------------------------------

def read_sc16(path):
    raw = np.fromfile(path, dtype=np.int16)
    if len(raw) % 2 != 0:
        raw = raw[:-1]
    iq = raw[0::2].astype(np.float32) + 1j * raw[1::2].astype(np.float32)
    return iq / 32768.0


def compute_psd(iq, sample_rate, fft_size=4096):
    num_segments = max(1, len(iq) // fft_size)
    truncated = iq[: num_segments * fft_size]
    segments = truncated.reshape(num_segments, fft_size)
    window = np.hanning(fft_size)
    spectra = np.fft.fftshift(np.fft.fft(segments * window, axis=1), axes=1)
    psd_db = 10 * np.log10(np.mean(np.abs(spectra) ** 2, axis=0) + 1e-20)
    freqs = np.linspace(-sample_rate / 2, sample_rate / 2, fft_size)
    return freqs, psd_db


def compute_signal_stats(iq, sample_rate, freqs, psd_db):
    i_data, q_data = iq.real, iq.imag
    rms = np.sqrt(np.mean(np.abs(iq) ** 2))
    peak = np.max(np.abs(iq))
    crest_factor_db = 20 * np.log10(peak / rms + 1e-20)
    dc_i, dc_q = float(np.mean(i_data)), float(np.mean(q_data))
    dc_mag = np.sqrt(dc_i ** 2 + dc_q ** 2)
    gain_i, gain_q = np.std(i_data), np.std(q_data)
    gain_imb = 20 * np.log10(gain_i / (gain_q + 1e-20))
    i_c, q_c = i_data - dc_i, q_data - dc_q
    corr = np.mean(i_c * q_c) / (np.std(i_c) * np.std(q_c) + 1e-20)
    phase_imb = np.degrees(np.arcsin(np.clip(corr, -1, 1)))
    peak_bin = np.argmax(psd_db)
    freq_offset = freqs[peak_bin]
    noise_floor = float(np.median(psd_db))
    snr = float(psd_db[peak_bin]) - noise_floor
    threshold = psd_db[peak_bin] - 10
    occ = psd_db >= threshold
    occ_bw = float(freqs[occ][-1] - freqs[occ][0]) if np.any(occ) else 0.0
    return {
        "rms_dbfs": float(20 * np.log10(rms + 1e-20)),
        "peak_dbfs": float(20 * np.log10(peak + 1e-20)),
        "crest_factor_db": float(crest_factor_db),
        "dc_offset_i": dc_i, "dc_offset_q": dc_q, "dc_magnitude": float(dc_mag),
        "gain_imbalance_db": float(gain_imb), "phase_imbalance_deg": float(phase_imb),
        "freq_offset_hz": float(freq_offset),
        "snr_estimate_db": float(snr), "noise_floor_db": noise_floor,
        "occupied_bw_hz": occ_bw,
        "duration_s": float(len(iq) / sample_rate),
        "num_samples": int(len(iq)), "sample_rate_hz": int(sample_rate),
    }


def read_wav_mono(path):
    sr, data = wavfile.read(path)
    if data.dtype == np.int16:
        s = data.astype(np.float32) / 32768.0
    elif data.dtype == np.int32:
        s = data.astype(np.float32) / 2147483648.0
    else:
        s = data.astype(np.float32)
    if s.ndim > 1:
        s = s[:, 0]
    return s, sr


# ---------------------------------------------------------------------------
# Plot helpers — return SVG string instead of writing to file
# ---------------------------------------------------------------------------

def fig_to_svg(fig, save_path=None):
    buf = BytesIO()
    fig.savefig(buf, format="svg")
    plt.close(fig)
    svg_str = buf.getvalue().decode("utf-8")
    if save_path:
        with open(save_path, "w") as f:
            f.write(svg_str)
    return svg_str


def make_spectrogram_svg(iq, sample_rate, fft_size=1024, save_path=None):
    fig, ax = plt.subplots(figsize=(12, 5))
    ax.specgram(iq, NFFT=fft_size, Fs=sample_rate / 1e3, noverlap=fft_size // 2,
                cmap="viridis", scale="dB", vmin=-80, vmax=0, Fc=0)
    ax.set_xlabel("Time (s)")
    ax.set_ylabel("Frequency (kHz)")
    ax.set_title("I/Q Spectrogram")
    fig.colorbar(ax.images[0], ax=ax, label="Power (dB)")
    fig.tight_layout()
    return fig_to_svg(fig, save_path)


def make_psd_svg(freqs, psd_db, stats, save_path=None):
    fig, ax = plt.subplots(figsize=(12, 4))
    ax.plot(freqs / 1e3, psd_db, linewidth=0.5)
    ax.axhline(stats["noise_floor_db"], color="red", linestyle="--", alpha=0.5,
               label=f"Noise floor: {stats['noise_floor_db']:.1f} dB")
    ax.axvline(stats["freq_offset_hz"] / 1e3, color="green", linestyle="--", alpha=0.5,
               label=f"Peak: {stats['freq_offset_hz']:.0f} Hz")
    threshold = np.max(psd_db) - 10
    mask = psd_db >= threshold
    if np.any(mask):
        ax.fill_between(freqs / 1e3, ax.get_ylim()[0], psd_db, where=mask, alpha=0.15, color="green",
                         label=f"Occupied BW: {stats['occupied_bw_hz']:.0f} Hz")
    ax.legend(fontsize=8)
    ax.set_xlabel("Frequency (kHz)")
    ax.set_ylabel("Power (dBFS)")
    ax.set_title("Power Spectral Density")
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    return fig_to_svg(fig, save_path)


def make_constellation_svg(iq, max_points=10000, save_path=None):
    if len(iq) > max_points:
        idx = np.random.default_rng(42).choice(len(iq), max_points, replace=False)
        iq_sub = iq[idx]
    else:
        iq_sub = iq
    fig, ax = plt.subplots(figsize=(5, 5))
    ax.scatter(iq_sub.real, iq_sub.imag, s=4, alpha=0.5, c="steelblue", edgecolors="none")
    ax.set_xlabel("I")
    ax.set_ylabel("Q")
    ax.set_title("Constellation")
    ax.set_aspect("equal")
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    return fig_to_svg(fig, save_path)


def make_audio_spectrogram_svg(samples, sample_rate, save_path=None):
    fig, ax = plt.subplots(figsize=(12, 4))
    ax.specgram(samples, NFFT=512, Fs=sample_rate / 1e3, noverlap=256,
                cmap="inferno", scale="dB", vmin=-80, vmax=0)
    ax.set_xlabel("Time (s)")
    ax.set_ylabel("Frequency (kHz)")
    ax.set_title("Audio Spectrogram")
    fig.colorbar(ax.images[0], ax=ax, label="Power (dB)")
    fig.tight_layout()
    return fig_to_svg(fig, save_path)


def make_audio_waveform_svg(samples, sample_rate, save_path=None):
    t = np.arange(len(samples)) / sample_rate
    fig, ax = plt.subplots(figsize=(12, 3))
    ax.plot(t, samples, linewidth=0.3, color="steelblue")
    ax.set_xlabel("Time (s)")
    ax.set_ylabel("Amplitude")
    ax.set_title("Audio Waveform")
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    return fig_to_svg(fig, save_path)


def make_loopback_svg(input_samples, input_sr, output_samples, output_sr,
                      overlay_save_path=None, corr_save_path=None):
    from scipy.signal import resample, correlate

    if input_sr != output_sr:
        output_samples = resample(output_samples, int(len(output_samples) * input_sr / output_sr))
    n = min(len(input_samples), len(output_samples))
    t = np.arange(n) / input_sr

    # Overlay
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(12, 5), sharex=True)
    ax1.plot(t, input_samples[:n], linewidth=0.3, color="steelblue", label="Input (TX)")
    ax1.set_ylabel("Amplitude")
    ax1.set_title("Loopback: Input vs Output")
    ax1.legend(loc="upper right")
    ax1.grid(True, alpha=0.3)
    ax2.plot(t, output_samples[:n], linewidth=0.3, color="coral", label="Output (RX)")
    ax2.set_ylabel("Amplitude")
    ax2.set_xlabel("Time (s)")
    ax2.legend(loc="upper right")
    ax2.grid(True, alpha=0.3)
    fig.tight_layout()
    overlay_svg = fig_to_svg(fig, overlay_save_path)

    # Cross-correlation
    a = input_samples[:n]
    b = output_samples[:n]
    a = (a - np.mean(a)) / (np.std(a) + 1e-10)
    b = (b - np.mean(b)) / (np.std(b) + 1e-10)
    max_lag_samples = int(0.1 * input_sr)
    corr = correlate(b, a, mode="full") / n
    center = len(a) - 1
    s, e = max(0, center - max_lag_samples), min(len(corr), center + max_lag_samples + 1)
    cw = corr[s:e]
    lags_ms = np.arange(s - center, e - center) / input_sr * 1000
    peak_idx = np.argmax(cw)
    peak_val, peak_lag = float(cw[peak_idx]), float(lags_ms[peak_idx])

    fig, ax = plt.subplots(figsize=(12, 4))
    ax.plot(lags_ms, cw, linewidth=0.8, color="steelblue")
    ax.axvline(peak_lag, color="red", linestyle="--", alpha=0.7,
               label=f"Peak: {peak_val:.3f} at {peak_lag:.1f} ms")
    ax.set_xlabel("Lag (ms)")
    ax.set_ylabel("Normalized Cross-Correlation")
    ax.set_title("Loopback Cross-Correlation")
    ax.legend()
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    corr_svg = fig_to_svg(fig, corr_save_path)

    return overlay_svg, corr_svg, peak_val, peak_lag


def make_psd_animation(iq, sample_rate, save_path, audio_path=None, fft_size=4096):
    """Render realtime PSD evolution MP4 with spectrogram reveal and optional audio."""
    from matplotlib.patches import Rectangle
    from matplotlib.ticker import FuncFormatter

    target_frames = 500
    segment_samples = max(fft_size, len(iq) // target_frames)
    num_segments = len(iq) // segment_samples
    if num_segments < 2:
        return None

    duration = len(iq) / sample_rate
    fps = num_segments / duration

    window = np.hanning(fft_size)
    freqs_khz = np.linspace(-sample_rate / 2, sample_rate / 2, fft_size) / 1e3

    psd_frames = []
    for i in range(num_segments):
        seg = iq[i * segment_samples : (i + 1) * segment_samples]
        n_sub = max(1, len(seg) // fft_size)
        truncated = seg[: n_sub * fft_size].reshape(n_sub, fft_size)
        spectra = np.fft.fftshift(np.fft.fft(truncated * window, axis=1), axes=1)
        psd_frames.append(10 * np.log10(np.mean(np.abs(spectra) ** 2, axis=0) + 1e-20))

    psd_all = np.array(psd_frames)
    psd_min, psd_max = np.min(psd_all) - 3, np.max(psd_all) + 3

    fig, (ax_spec, ax_psd) = plt.subplots(2, 1, figsize=(10, 7),
                                           gridspec_kw={"height_ratios": [1, 1.2]})
    # Fs=sample_rate so time axis is in seconds (not ms)
    Pxx, _, _, im_spec = ax_spec.specgram(iq, NFFT=1024, Fs=sample_rate, noverlap=512,
                                          cmap="viridis", scale="dB", Fc=0)
    spec_db = 10 * np.log10(Pxx + 1e-20)
    im_spec.set_clim(vmin=np.percentile(spec_db, 2), vmax=np.percentile(spec_db, 99.5))
    ax_spec.yaxis.set_major_formatter(FuncFormatter(lambda x, _: f"{x / 1e3:.0f}"))
    ax_spec.set_ylabel("Frequency (kHz)")
    ax_spec.set_title("PSD Evolution")
    ax_spec.set_xlim(0, duration)

    # White overlay for progressive reveal — coordinates in seconds
    ylim = ax_spec.get_ylim()
    overlay = ax_spec.add_patch(
        Rectangle((0, ylim[0]), duration, ylim[1] - ylim[0],
                  facecolor="white", alpha=0.75, zorder=5))
    cursor = ax_spec.axvline(0, color="red", linewidth=2, alpha=0.9, zorder=6)

    line, = ax_psd.plot(freqs_khz, psd_frames[0], linewidth=0.8, color="steelblue")
    ax_psd.set_xlim(freqs_khz[0], freqs_khz[-1])
    ax_psd.set_ylim(psd_min, psd_max)
    ax_psd.set_xlabel("Frequency (kHz)")
    ax_psd.set_ylabel("Power (dBFS)")
    ax_psd.grid(True, alpha=0.3)
    time_text = ax_psd.text(0.02, 0.95, "", transform=ax_psd.transAxes, fontsize=10,
                            verticalalignment="top",
                            bbox=dict(boxstyle="round,pad=0.3", facecolor="white", alpha=0.8))
    fig.tight_layout()

    def update(frame):
        t_sec = (frame + 0.5) * segment_samples / sample_rate
        overlay.set_x(t_sec)
        overlay.set_width(max(0, duration - t_sec))
        cursor.set_xdata([t_sec, t_sec])
        line.set_ydata(psd_frames[frame])
        time_text.set_text(f"t = {t_sec:.3f} s")

    anim = animation.FuncAnimation(fig, update, frames=num_segments,
                                   interval=1000 / fps, blit=False)
    writer = animation.FFMpegWriter(fps=fps, bitrate=2000,
                                    extra_args=["-pix_fmt", "yuv420p"])

    if audio_path and os.path.isfile(audio_path):
        tmp_fd, tmp_video = tempfile.mkstemp(suffix=".mp4")
        os.close(tmp_fd)
        try:
            anim.save(tmp_video, writer=writer)
            plt.close(fig)
            cmd = ["ffmpeg", "-y", "-i", tmp_video, "-i", audio_path,
                   "-c:v", "copy", "-c:a", "aac", "-b:a", "128k",
                   "-shortest", "-movflags", "+faststart", save_path]
            result = subprocess.run(cmd, capture_output=True, text=True)
            if result.returncode != 0:
                os.rename(tmp_video, save_path)
                tmp_video = None
        finally:
            if tmp_video and os.path.exists(tmp_video):
                os.unlink(tmp_video)
    else:
        anim.save(save_path, writer=writer)
        plt.close(fig)

    return save_path


# ---------------------------------------------------------------------------
# HTML template
# ---------------------------------------------------------------------------

HTML_TEMPLATE = textwrap.dedent("""\
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>PRETTY SDR Report — {title}</title>
<style>
  body {{ font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Helvetica, Arial, sans-serif;
         max-width: 1200px; margin: 0 auto; padding: 20px; background: #fafafa; color: #333; }}
  h1 {{ border-bottom: 2px solid #333; padding-bottom: 8px; }}
  h2 {{ margin-top: 40px; color: #555; }}
  h3 {{ color: #666; }}
  .capture {{ border: 1px solid #ddd; border-radius: 8px; padding: 20px; margin: 20px 0;
              background: white; box-shadow: 0 1px 3px rgba(0,0,0,0.1); }}
  .stats {{ display: grid; grid-template-columns: repeat(auto-fill, minmax(280px, 1fr));
            gap: 10px; margin: 15px 0; }}
  .stat {{ background: #f5f5f5; padding: 8px 12px; border-radius: 4px; font-size: 14px; }}
  .stat .label {{ color: #888; font-size: 12px; }}
  .stat .value {{ font-weight: 600; font-size: 16px; }}
  .plots {{ display: grid; grid-template-columns: 1fr; gap: 10px; }}
  .plots svg {{ width: 100%; height: auto; }}
  .side-by-side {{ display: grid; grid-template-columns: 1fr 1fr; gap: 10px; }}
  .log {{ background: #f8f8f8; border: 1px solid #eee; border-radius: 4px; padding: 12px;
          font-family: monospace; font-size: 12px; white-space: pre-wrap; max-height: 300px;
          overflow-y: auto; }}
  .badge {{ display: inline-block; padding: 2px 8px; border-radius: 4px; font-size: 12px;
            font-weight: 600; }}
  .badge-pass {{ background: #d4edda; color: #155724; }}
  .badge-warn {{ background: #fff3cd; color: #856404; }}
  .badge-fail {{ background: #f8d7da; color: #721c24; }}
  footer {{ margin-top: 40px; padding-top: 10px; border-top: 1px solid #ddd;
            color: #999; font-size: 12px; }}
</style>
</head>
<body>
<h1>PRETTY SDR Report</h1>
<p><strong>{title}</strong> — generated {timestamp}</p>
{content}
<footer>OPS-SAT PRETTY Experiment — Ground Analysis Tools</footer>
</body>
</html>
""")


def stat_html(label, value):
    return f'<div class="stat"><div class="label">{label}</div><div class="value">{value}</div></div>'


def badge(text, level):
    return f'<span class="badge badge-{level}">{text}</span>'


# ---------------------------------------------------------------------------
# Report builders
# ---------------------------------------------------------------------------

def process_capture_dir(capture_dir, sample_rate, output_dir):
    """Process a single capture directory (sc16 + wav + log). Returns HTML fragment."""
    sc16_files = sorted(glob.glob(os.path.join(capture_dir, "*.sc16")))
    wav_files = sorted(glob.glob(os.path.join(capture_dir, "*.wav")))
    log_files = sorted(glob.glob(os.path.join(capture_dir, "*.log")))

    name = os.path.basename(capture_dir)
    svg_dir = os.path.join(output_dir, name)
    os.makedirs(svg_dir, exist_ok=True)

    parts = [f'<div class="capture"><h3>{name}</h3>']

    for sc16 in sc16_files:
        print(f"  Processing {sc16}...")
        iq = read_sc16(sc16)
        freqs, psd_db = compute_psd(iq, sample_rate)
        stats = compute_signal_stats(iq, sample_rate, freqs, psd_db)

        snr_badge = badge(f"SNR {stats['snr_estimate_db']:.1f} dB",
                          "pass" if stats['snr_estimate_db'] > 10 else
                          "warn" if stats['snr_estimate_db'] > 3 else "fail")

        parts.append(f'<h4>{os.path.basename(sc16)} {snr_badge}</h4>')
        parts.append('<div class="stats">')
        parts.append(stat_html("Duration", f"{stats['duration_s']:.2f} s"))
        parts.append(stat_html("RMS Level", f"{stats['rms_dbfs']:.1f} dBFS"))
        parts.append(stat_html("Peak Level", f"{stats['peak_dbfs']:.1f} dBFS"))
        parts.append(stat_html("Crest Factor", f"{stats['crest_factor_db']:.1f} dB"))
        parts.append(stat_html("SNR Estimate", f"{stats['snr_estimate_db']:.1f} dB"))
        parts.append(stat_html("Noise Floor", f"{stats['noise_floor_db']:.1f} dB"))
        parts.append(stat_html("Freq Offset", f"{stats['freq_offset_hz']:.0f} Hz"))
        parts.append(stat_html("Occupied BW", f"{stats['occupied_bw_hz']:.0f} Hz"))
        parts.append(stat_html("DC Offset (I/Q)", f"{stats['dc_offset_i']:.4f} / {stats['dc_offset_q']:.4f}"))
        parts.append(stat_html("Gain Imbalance", f"{stats['gain_imbalance_db']:.2f} dB"))
        parts.append(stat_html("Phase Imbalance", f"{stats['phase_imbalance_deg']:.2f}°"))
        parts.append('</div>')

        parts.append('<div class="plots">')
        parts.append(make_spectrogram_svg(iq, sample_rate, save_path=os.path.join(svg_dir, "spectrogram.svg")))
        parts.append(make_psd_svg(freqs, psd_db, stats, save_path=os.path.join(svg_dir, "psd.svg")))
        parts.append('</div>')
        parts.append('<div class="side-by-side">')
        parts.append(make_constellation_svg(iq, save_path=os.path.join(svg_dir, "constellation.svg")))
        parts.append('</div>')

        # PSD evolution animation (with audio if available)
        print(f"  Rendering PSD animation...")
        mp4_path = os.path.join(svg_dir, "psd_evolution.mp4")
        wav_for_audio = wav_files[0] if wav_files else None
        if make_psd_animation(iq, sample_rate, mp4_path, audio_path=wav_for_audio):
            rel_path = f"{name}/psd_evolution.mp4"
            parts.append(f'<video controls width="100%" style="margin:10px 0">'
                         f'<source src="{rel_path}" type="video/mp4">'
                         f'PSD evolution animation</video>')

        # Save stats
        with open(os.path.join(svg_dir, "iq_stats.json"), "w") as f:
            json.dump(stats, f, indent=2)

    for wav in wav_files:
        print(f"  Processing {wav}...")
        samples, sr = read_wav_mono(wav)
        parts.append(f'<h4>{os.path.basename(wav)}</h4>')
        parts.append('<div class="plots">')
        parts.append(make_audio_spectrogram_svg(samples, sr, save_path=os.path.join(svg_dir, "audio_spectrogram.svg")))
        parts.append(make_audio_waveform_svg(samples, sr, save_path=os.path.join(svg_dir, "audio_waveform.svg")))
        parts.append('</div>')

    for log in log_files:
        try:
            with open(log, "r") as f:
                log_text = f.read()
            if log_text.strip():
                parts.append(f'<details><summary>{os.path.basename(log)}</summary>')
                parts.append(f'<div class="log">{log_text}</div></details>')
        except Exception:
            pass

    parts.append('</div>')
    return "\n".join(parts)


def build_capture_report(run_dir, sample_rate, output_dir):
    """Build report for an sdr-capture run directory."""
    content_parts = []

    # Check for capture subdirectories (capture-001, capture-002, ...)
    capture_dirs = sorted(glob.glob(os.path.join(run_dir, "capture-*")))
    if capture_dirs:
        for cd in capture_dirs:
            content_parts.append(process_capture_dir(cd, sample_rate, output_dir))
    else:
        # Flat structure (sc16/wav files directly in run dir)
        content_parts.append(process_capture_dir(run_dir, sample_rate, output_dir))

    # Summary file
    summary_path = os.path.join(run_dir, "summary.txt")
    if os.path.isfile(summary_path):
        with open(summary_path, "r") as f:
            summary = f.read()
        content_parts.append(f'<h2>Run Summary</h2><div class="log">{summary}</div>')

    # Run log
    run_log = os.path.join(run_dir, "run.log")
    if os.path.isfile(run_log):
        with open(run_log, "r") as f:
            log_text = f.read()
        content_parts.append(f'<details><summary>run.log</summary><div class="log">{log_text}</div></details>')

    # PSD comparison across captures
    all_sc16 = sorted(glob.glob(os.path.join(run_dir, "**", "*.sc16"), recursive=True))
    if len(all_sc16) > 1:
        print("  Generating PSD comparison...")
        fig, ax = plt.subplots(figsize=(12, 4))
        for sc16 in all_sc16:
            iq = read_sc16(sc16)
            freqs, psd_db = compute_psd(iq, sample_rate)
            label = os.path.basename(os.path.dirname(sc16))
            ax.plot(freqs / 1e3, psd_db, linewidth=0.6, label=label, alpha=0.8)
        ax.set_xlabel("Frequency (kHz)")
        ax.set_ylabel("Power (dBFS)")
        ax.set_title("PSD Comparison Across Captures")
        ax.grid(True, alpha=0.3)
        ax.legend(fontsize=8)
        fig.tight_layout()
        content_parts.insert(0, '<h2>PSD Comparison</h2>' +
                             fig_to_svg(fig, os.path.join(output_dir, "psd_comparison.svg")))

    return "\n".join(content_parts)


def build_loopback_report(run_dir, sample_rate, output_dir, input_wav=None):
    """Build report for an sdr-loopback run directory."""
    content_parts = []

    sc16_files = sorted(glob.glob(os.path.join(run_dir, "*.sc16")))
    wav_files = sorted(glob.glob(os.path.join(run_dir, "*.wav")))

    # I/Q analysis
    for sc16 in sc16_files:
        print(f"  Processing {sc16}...")
        iq = read_sc16(sc16)
        freqs, psd_db = compute_psd(iq, sample_rate)
        stats = compute_signal_stats(iq, sample_rate, freqs, psd_db)

        snr_badge = badge(f"SNR {stats['snr_estimate_db']:.1f} dB",
                          "pass" if stats['snr_estimate_db'] > 10 else
                          "warn" if stats['snr_estimate_db'] > 3 else "fail")

        content_parts.append(f'<div class="capture"><h3>{os.path.basename(sc16)} {snr_badge}</h3>')
        content_parts.append('<div class="stats">')
        content_parts.append(stat_html("Duration", f"{stats['duration_s']:.2f} s"))
        content_parts.append(stat_html("RMS Level", f"{stats['rms_dbfs']:.1f} dBFS"))
        content_parts.append(stat_html("SNR Estimate", f"{stats['snr_estimate_db']:.1f} dB"))
        content_parts.append(stat_html("Freq Offset", f"{stats['freq_offset_hz']:.0f} Hz"))
        content_parts.append(stat_html("Occupied BW", f"{stats['occupied_bw_hz']:.0f} Hz"))
        content_parts.append(stat_html("DC Offset (I/Q)", f"{stats['dc_offset_i']:.4f} / {stats['dc_offset_q']:.4f}"))
        content_parts.append(stat_html("Gain Imbalance", f"{stats['gain_imbalance_db']:.2f} dB"))
        content_parts.append(stat_html("Phase Imbalance", f"{stats['phase_imbalance_deg']:.2f}°"))
        content_parts.append('</div>')
        content_parts.append('<div class="plots">')
        content_parts.append(make_spectrogram_svg(iq, sample_rate, save_path=os.path.join(output_dir, "spectrogram.svg")))
        content_parts.append(make_psd_svg(freqs, psd_db, stats, save_path=os.path.join(output_dir, "psd.svg")))
        content_parts.append('</div>')
        content_parts.append('<div class="side-by-side">')
        content_parts.append(make_constellation_svg(iq, save_path=os.path.join(output_dir, "constellation.svg")))
        content_parts.append('</div>')

        # PSD evolution animation (with audio if available)
        print(f"  Rendering PSD animation...")
        mp4_path = os.path.join(output_dir, "psd_evolution.mp4")
        wav_for_audio = wav_files[0] if wav_files else None
        if make_psd_animation(iq, sample_rate, mp4_path, audio_path=wav_for_audio):
            content_parts.append('<video controls width="100%" style="margin:10px 0">'
                                 '<source src="psd_evolution.mp4" type="video/mp4">'
                                 'PSD evolution animation</video>')

        content_parts.append('</div>')

        with open(os.path.join(output_dir, "iq_stats.json"), "w") as f:
            json.dump(stats, f, indent=2)

    # Audio analysis
    for wav in wav_files:
        print(f"  Processing {wav}...")
        samples, sr = read_wav_mono(wav)
        content_parts.append(f'<div class="capture"><h3>{os.path.basename(wav)}</h3>')
        content_parts.append('<div class="plots">')
        content_parts.append(make_audio_spectrogram_svg(samples, sr, save_path=os.path.join(output_dir, "audio_spectrogram.svg")))
        content_parts.append(make_audio_waveform_svg(samples, sr, save_path=os.path.join(output_dir, "audio_waveform.svg")))
        content_parts.append('</div>')

        # Loopback comparison if input WAV provided
        if input_wav and os.path.isfile(input_wav):
            print(f"  Comparing with input {input_wav}...")
            input_samples, input_sr = read_wav_mono(input_wav)
            overlay_svg, corr_svg, peak_val, peak_lag = make_loopback_svg(
                input_samples, input_sr, samples, sr,
                overlay_save_path=os.path.join(output_dir, "loopback_overlay.svg"),
                corr_save_path=os.path.join(output_dir, "loopback_correlation.svg"))

            corr_level = "pass" if peak_val >= 0.7 else "warn" if peak_val >= 0.3 else "fail"
            corr_text = "PASS" if peak_val >= 0.7 else "WARN" if peak_val >= 0.3 else "FAIL"
            content_parts.append(f'<h4>Loopback Quality {badge(f"{corr_text}: {peak_val:.3f}", corr_level)}</h4>')
            content_parts.append('<div class="plots">')
            content_parts.append(overlay_svg)
            content_parts.append(corr_svg)
            content_parts.append('</div>')

        content_parts.append('</div>')

    # Summary and logs
    summary_path = os.path.join(run_dir, "summary.txt")
    if os.path.isfile(summary_path):
        with open(summary_path, "r") as f:
            content_parts.append(f'<h2>Run Summary</h2><div class="log">{f.read()}</div>')

    run_log = os.path.join(run_dir, "run.log")
    if os.path.isfile(run_log):
        with open(run_log, "r") as f:
            content_parts.append(f'<details><summary>run.log</summary><div class="log">{f.read()}</div></details>')

    return "\n".join(content_parts)


def main():
    parser = argparse.ArgumentParser(description="Generate HTML report for SDR capture/loopback run")
    parser.add_argument("run_dir", help="Path to run directory (e.g. toGround/run-000001)")
    parser.add_argument("--output-dir", default=".", help="Output directory for report.html")
    parser.add_argument("--sample-rate", type=int, default=200000,
                        help="I/Q sample rate in Hz (default: 200000 = effective rate after decimation)")
    parser.add_argument("--loopback", action="store_true", help="Treat as loopback run (flat file structure)")
    parser.add_argument("--input-wav", help="Input WAV for loopback comparison")
    args = parser.parse_args()

    if not os.path.isdir(args.run_dir):
        print(f"Error: {args.run_dir} is not a directory", file=sys.stderr)
        return 1

    output_dir = resolve_output_dir(args.output_dir, args.run_dir)
    title = os.path.basename(args.run_dir)
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    print(f"Generating report for {args.run_dir}...")

    if args.loopback:
        content = build_loopback_report(args.run_dir, args.sample_rate, output_dir, args.input_wav)
    else:
        content = build_capture_report(args.run_dir, args.sample_rate, output_dir)

    html = HTML_TEMPLATE.format(title=title, timestamp=timestamp, content=content)

    output_path = os.path.join(output_dir, "index.html")
    with open(output_path, "w") as f:
        f.write(html)

    print(f"\nReport: {output_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
