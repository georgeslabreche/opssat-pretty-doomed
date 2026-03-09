"""Shared I/Q and audio data I/O and analysis functions."""
import numpy as np
from scipy.io import wavfile


def read_sc16(path):
    """Read sc16 file: interleaved int16 (I, Q, I, Q, ...), return complex64."""
    raw = np.fromfile(path, dtype=np.int16)
    if len(raw) % 2 != 0:
        raw = raw[:-1]
    iq = raw[0::2].astype(np.float32) + 1j * raw[1::2].astype(np.float32)
    return iq / 32768.0


def read_wav_mono(path):
    """Read WAV, return (samples_float32, sample_rate)."""
    sr, data = wavfile.read(path)
    if data.dtype == np.int16:
        samples = data.astype(np.float32) / 32768.0
    elif data.dtype == np.int32:
        samples = data.astype(np.float32) / 2147483648.0
    else:
        samples = data.astype(np.float32)
    if samples.ndim > 1:
        samples = samples[:, 0]
    return samples, sr


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

    rms = np.sqrt(np.mean(np.abs(iq) ** 2))
    peak = np.max(np.abs(iq))
    crest_factor_db = 20 * np.log10(peak / rms + 1e-20)

    dc_i = np.mean(i_data)
    dc_q = np.mean(q_data)
    dc_magnitude = np.sqrt(dc_i ** 2 + dc_q ** 2)

    gain_i = np.std(i_data)
    gain_q = np.std(q_data)
    gain_imbalance_db = 20 * np.log10(gain_i / (gain_q + 1e-20))
    i_centered = i_data - dc_i
    q_centered = q_data - dc_q
    correlation = np.mean(i_centered * q_centered) / (np.std(i_centered) * np.std(q_centered) + 1e-20)
    phase_imbalance_deg = np.degrees(np.arcsin(np.clip(correlation, -1, 1)))

    peak_bin = np.argmax(psd_db)
    freq_offset_hz = freqs[peak_bin]
    noise_floor_db = np.median(psd_db)
    peak_power_db = psd_db[peak_bin]
    snr_db = peak_power_db - noise_floor_db

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
