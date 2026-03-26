"""Shared plot functions. Each returns an SVG string and optionally saves to file."""
from io import BytesIO

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from scipy.signal import correlate, resample


def fig_to_svg(fig, save_path=None):
    """Render figure to SVG string, optionally saving to file. Closes the figure."""
    buf = BytesIO()
    fig.savefig(buf, format="svg")
    plt.close(fig)
    svg_str = buf.getvalue().decode("utf-8")
    if save_path:
        with open(save_path, "w") as f:
            f.write(svg_str)
    return svg_str


def plot_spectrogram(iq, sample_rate, save_path=None, fft_size=1024):
    """I/Q spectrogram waterfall. Returns SVG string."""
    fig, ax = plt.subplots(figsize=(12, 6))
    ax.specgram(iq, NFFT=fft_size, Fs=sample_rate / 1e3, noverlap=fft_size // 2,
                cmap="viridis", scale="dB", vmin=-80, vmax=0, Fc=0)
    ax.set_xlabel("Time (s)")
    ax.set_ylabel("Frequency (kHz)")
    ax.set_title("I/Q Spectrogram")
    fig.colorbar(ax.images[0], ax=ax, label="Power (dB)")
    fig.tight_layout()
    return fig_to_svg(fig, save_path)


def plot_psd(freqs, psd_db, save_path=None, stats=None):
    """Averaged PSD with optional annotations. Returns SVG string."""
    fig, ax = plt.subplots(figsize=(12, 5))
    ax.plot(freqs / 1e3, psd_db, linewidth=0.5)

    if stats:
        ax.axhline(stats["noise_floor_db"], color="red", linestyle="--", alpha=0.5,
                    label=f"Noise floor: {stats['noise_floor_db']:.1f} dB")
        ax.axvline(stats["freq_offset_hz"] / 1e3, color="green", linestyle="--", alpha=0.5,
                    label=f"Peak: {stats['freq_offset_hz']:.0f} Hz")
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
    return fig_to_svg(fig, save_path)


def plot_constellation(iq, save_path=None, max_points=10000):
    """I/Q scatter plot. Returns SVG string."""
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
    return fig_to_svg(fig, save_path)


def plot_waveform(iq, sample_rate, save_path=None, max_seconds=0.01):
    """Time-domain I and Q waveform. Returns SVG string."""
    max_samples = int(max_seconds * sample_rate)
    iq_sub = iq[:max_samples]
    t = np.arange(len(iq_sub)) / sample_rate * 1e3

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
    return fig_to_svg(fig, save_path)


def plot_iq_amplitude(iq, sample_rate, save_path=None, window_ms=100):
    """I and Q channel RMS amplitude over time (windowed). Returns SVG string."""
    window = int(window_ms * sample_rate / 1000)
    if window < 1:
        window = 1
    n_windows = len(iq) // window
    if n_windows < 1:
        return ""

    i_data = iq[:n_windows * window].real.reshape(n_windows, window)
    q_data = iq[:n_windows * window].imag.reshape(n_windows, window)

    rms_i = np.sqrt(np.mean(i_data ** 2, axis=1))
    rms_q = np.sqrt(np.mean(q_data ** 2, axis=1))
    t = (np.arange(n_windows) + 0.5) * window / sample_rate

    fig, ax = plt.subplots(figsize=(12, 5))
    ax.plot(t, rms_i, linewidth=0.8, color="steelblue", label="I RMS", alpha=0.8)
    ax.plot(t, rms_q, linewidth=0.8, color="coral", label="Q RMS", alpha=0.8)
    ax.set_xlabel("Time (s)")
    ax.set_ylabel("RMS Amplitude")
    ax.set_title(f"I/Q Channel Amplitude ({window_ms} ms windows)")
    ax.legend()
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    return fig_to_svg(fig, save_path)


def plot_audio_spectrogram(samples, sample_rate, save_path=None, fft_size=512):
    """Audio spectrogram. Returns SVG string."""
    fig, ax = plt.subplots(figsize=(12, 5))
    ax.specgram(samples, NFFT=fft_size, Fs=sample_rate / 1e3, noverlap=fft_size // 2,
                cmap="inferno", scale="dB", vmin=-80, vmax=0)
    ax.set_xlabel("Time (s)")
    ax.set_ylabel("Frequency (kHz)")
    ax.set_title("Audio Spectrogram")
    fig.colorbar(ax.images[0], ax=ax, label="Power (dB)")
    fig.tight_layout()
    return fig_to_svg(fig, save_path)


def plot_audio_waveform(samples, sample_rate, save_path=None):
    """Audio amplitude over time. Returns SVG string."""
    t = np.arange(len(samples)) / sample_rate
    fig, ax = plt.subplots(figsize=(12, 4))
    ax.plot(t, samples, linewidth=0.3, color="steelblue")
    ax.set_xlabel("Time (s)")
    ax.set_ylabel("Amplitude")
    ax.set_title("Audio Waveform")
    ax.set_xlim(0, t[-1])
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    return fig_to_svg(fig, save_path)


def plot_loopback(input_samples, input_sr, output_samples, output_sr,
                  overlay_save_path=None, corr_save_path=None):
    """Loopback overlay + cross-correlation. Returns (overlay_svg, corr_svg, peak_val, peak_lag_ms)."""
    if input_sr != output_sr:
        output_samples = resample(output_samples, int(len(output_samples) * input_sr / output_sr))
    n = min(len(input_samples), len(output_samples))
    t = np.arange(n) / input_sr

    # Overlay
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(14, 6), sharex=True)
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

    fig, ax = plt.subplots(figsize=(12, 5))
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


def plot_psd_comparison(sc16_paths, sample_rate, save_path=None, fft_size=4096,
                        read_sc16_fn=None, compute_psd_fn=None, label_fn=None):
    """PSD comparison overlay from multiple sc16 files. Returns SVG string."""
    from iq import read_sc16, compute_psd
    _read = read_sc16_fn or read_sc16
    _psd = compute_psd_fn or compute_psd

    fig, ax = plt.subplots(figsize=(14, 6))
    for path in sc16_paths:
        iq = _read(path)
        freqs, psd_db = _psd(iq, sample_rate, fft_size)
        label = label_fn(path) if label_fn else _default_label(path)
        ax.plot(freqs / 1e3, psd_db, linewidth=0.6, label=label, alpha=0.8)

    ax.set_xlabel("Frequency (kHz)")
    ax.set_ylabel("Power (dBFS)")
    ax.set_title("PSD Comparison")
    ax.grid(True, alpha=0.3)
    ax.legend(fontsize=7, loc="upper right")
    fig.tight_layout()
    return fig_to_svg(fig, save_path)


def _default_label(path):
    """Create a short label from a file path."""
    import os
    parts = path.replace("\\", "/").split("/")
    interesting = []
    for p in parts:
        if p.startswith("run-") or p.startswith("capture-") or p.startswith("pack-"):
            interesting.append(p)
    if interesting:
        return "/".join(interesting)
    return os.path.basename(path)
