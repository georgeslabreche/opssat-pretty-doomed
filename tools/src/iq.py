"""Shared I/Q and audio data I/O and analysis functions."""
import numpy as np
from scipy import signal
from scipy.io import wavfile


def read_sc16(path):
    """Read sc16/cs16 file: interleaved little-endian int16 (I, Q, I, Q, ...),
    return complex64 normalized to +/-1.0."""
    raw = np.fromfile(path, dtype="<i2")
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

    rms_i = float(np.sqrt(np.mean(i_data ** 2)))
    rms_q = float(np.sqrt(np.mean(q_data ** 2)))
    n = len(iq)
    zero_i = int(np.sum(i_data == 0.0))
    zero_q = int(np.sum(q_data == 0.0))

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
        "rms_i": rms_i,
        "rms_q": rms_q,
        "rms_i_dbfs": float(20 * np.log10(rms_i + 1e-20)),
        "rms_q_dbfs": float(20 * np.log10(rms_q + 1e-20)),
        "zero_fraction_i": float(zero_i / n),
        "zero_fraction_q": float(zero_q / n),
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
        "Per-Channel",
        "-" * 40,
        f"RMS (I):           {stats['rms_i']:.4f} ({stats['rms_i_dbfs']:.1f} dBFS)",
        f"RMS (Q):           {stats['rms_q']:.4f} ({stats['rms_q_dbfs']:.1f} dBFS)",
        f"Zero fraction (I): {stats['zero_fraction_i']*100:.2f}%",
        f"Zero fraction (Q): {stats['zero_fraction_q']*100:.2f}%",
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


# --- Carrier detection -----------------------------------------------------
# Helpers for the RF-link test: find a narrowband carrier in a wideband capture,
# measure how far it stands above the noise floor, its width, and whether it
# keys on and off or drifts (Doppler) over the recording. The capture is tuned
# with the SDR center offset from the expected uplink so the carrier lands away
# from the DC spike; pass --center-freq / --target-freq to report absolute
# frequencies and the offset from the intended uplink.

def find_top_peaks(freqs, psd_db, n=8, dc_guard_hz=3000.0, min_separation_hz=15000.0):
    """Top-N spectral peaks outside a DC guard band, each separated from the
    others by min_separation_hz. Returns a list of dicts sorted by power:
    {offset_hz, power_db, snr_db}, where snr_db is power over the median floor."""
    floor = float(np.median(psd_db))
    order = np.argsort(psd_db)[::-1]
    peaks = []
    for i in order:
        fo = float(freqs[i])
        if abs(fo) <= dc_guard_hz:
            continue
        if any(abs(fo - p["offset_hz"]) < min_separation_hz for p in peaks):
            continue
        peaks.append({"offset_hz": fo, "power_db": float(psd_db[i]),
                      "snr_db": float(psd_db[i] - floor)})
        if len(peaks) >= n:
            break
    return peaks


def carrier_bandwidth(freqs, psd_db, carrier_hz, span_hz=40000.0):
    """-3 dB and -20 dB widths (Hz) around a carrier, measured on the PSD."""
    near = np.abs(freqs - carrier_hz) < span_hz
    if not np.any(near):
        return 0.0, 0.0
    fn, pn = freqs[near], psd_db[near]
    pk = float(pn.max())

    def width(drop):
        sel = fn[pn >= pk - drop]
        return float(sel.max() - sel.min()) if len(sel) else 0.0

    return width(3.0), width(20.0)


def track_tone(iq, sample_rate, fft_size=4096, dc_guard_hz=3000.0):
    """Per-time-slice strongest tone outside the DC guard. Returns
    (times_s, offsets_hz, snr_db) arrays; a drifting offset indicates Doppler,
    a fixed one an internal spur."""
    f, t, sxx = signal.spectrogram(iq, fs=sample_rate, nperseg=fft_size,
                                   noverlap=fft_size // 2, return_onesided=False,
                                   detrend=False)
    f = np.fft.fftshift(f)
    sdb = 10 * np.log10(np.fft.fftshift(sxx, axes=0) + 1e-20)
    guard = np.abs(f) > dc_guard_hz
    offs, snrs = [], []
    for ti in range(sdb.shape[1]):
        col = sdb[:, ti]
        idx = int(np.argmax(np.where(guard, col, -np.inf)))
        offs.append(float(f[idx]))
        snrs.append(float(col[idx] - np.median(col)))
    return t, np.array(offs), np.array(snrs)


def carrier_envelope(iq, sample_rate, carrier_hz, bw_hz=30000.0, bin_ms=1.0):
    """Downconvert to the carrier, low-pass, and return (times_s, env_db) at
    bin_ms resolution. The envelope shows keying (on/off) and fades."""
    t = np.arange(len(iq)) / sample_rate
    x = iq * np.exp(-1j * 2 * np.pi * carrier_hz * t)
    taps = signal.firwin(255, min(bw_hz / (sample_rate / 2), 0.99))
    env = np.abs(signal.lfilter(taps, 1, x))
    step = max(1, int(sample_rate * bin_ms / 1000))
    m = len(env) // step * step
    env_b = env[:m].reshape(-1, step).mean(axis=1)
    env_db = 20 * np.log10(env_b + 1e-9)
    tt = (np.arange(len(env_b)) + 0.5) * step / sample_rate
    return tt, env_db


def carrier_drift(iq, sample_rate, carrier_offset_hz, win_s=0.05, lp_hz=5000.0,
                  present_margin_db=6.0):
    """Track the carrier's frequency over time to measure any drift (Doppler
    rate). Mixes the carrier toward baseband, low-passes, then per short window
    takes the residual peak frequency. Fits a line over the windows where the
    carrier is present. Returns times_s, freqs_hz, present flags, and the fitted
    slope_hz_per_s and total span_hz. A static spur gives slope ~0; a
    ground signal received through a pass drifts at the Doppler rate."""
    t = np.arange(len(iq)) / sample_rate
    x = iq * np.exp(-1j * 2 * np.pi * carrier_offset_hz * t)
    taps = signal.firwin(401, min(lp_hz / (sample_rate / 2), 0.99))
    x = signal.lfilter(taps, 1, x)
    decim = max(1, int(sample_rate // (lp_hz * 5)))
    x = x[::decim]
    fsd = sample_rate / decim
    win = max(8, int(fsd * win_s))
    times, freqs_hz, strength = [], [], []
    for i in range(0, len(x) - win, win):
        seg = x[i:i + win] * np.hanning(win)
        spec = np.abs(np.fft.fftshift(np.fft.fft(seg)))
        ff = np.fft.fftshift(np.fft.fftfreq(win, 1 / fsd))
        pk = int(np.argmax(spec))
        times.append((i + win / 2) / fsd)
        freqs_hz.append(float(ff[pk]))
        strength.append(float(20 * np.log10(spec[pk] / (np.median(spec) + 1e-12))))
    times = np.array(times)
    freqs_hz = np.array(freqs_hz)
    strength = np.array(strength)
    present = strength > (strength.max() - present_margin_db) if len(strength) else np.array([])
    slope = float("nan")
    span = float("nan")
    if present.sum() >= 3:
        p = np.polyfit(times[present], freqs_hz[present], 1)
        slope = float(p[0])
        span = float(slope * (times[present].max() - times[present].min()))
    return {
        "times_s": times.tolist(),
        "freqs_hz": freqs_hz.tolist(),
        "present": present.tolist() if len(present) else [],
        "slope_hz_per_s": slope,
        "span_hz": span,
    }


def analyze_carrier(iq, sample_rate, freqs, psd_db, center_freq=None,
                    target_freq=None, dc_guard_hz=3000.0, on_threshold_db=10.0):
    """Locate the strongest non-DC carrier and characterize it. Returns a dict
    with its offset, absolute frequency and offset from the target (when
    center/target are given), SNR over the floor, -3/-20 dB widths, the on
    fraction, a coarse 100 ms on/off keying string, and the top peaks."""
    peaks = find_top_peaks(freqs, psd_db, dc_guard_hz=dc_guard_hz)
    carrier = peaks[0]
    fo = carrier["offset_hz"]
    bw3, bw20 = carrier_bandwidth(freqs, psd_db, fo)
    tt, env_db = carrier_envelope(iq, sample_rate, fo)
    on = env_db > (env_db.max() - on_threshold_db)
    # Coarse 100 ms on/off timeline for a quick keying view.
    bin_ct = max(1, int(round((tt[-1] - tt[0]) / 0.1))) if len(tt) > 1 else 1
    edges = np.linspace(0, len(env_db), bin_ct + 1, dtype=int)
    keying = "".join(
        "#" if env_db[a:b].mean() > (env_db.max() - on_threshold_db) else "."
        for a, b in zip(edges[:-1], edges[1:]) if b > a
    )
    drift = carrier_drift(iq, sample_rate, fo)
    report = {
        "sample_rate_hz": int(sample_rate),
        "duration_s": float(len(iq) / sample_rate),
        "num_samples": int(len(iq)),
        "center_freq_hz": float(center_freq) if center_freq else None,
        "target_freq_hz": float(target_freq) if target_freq else None,
        "carrier_offset_hz": fo,
        "carrier_freq_hz": float(center_freq + fo) if center_freq else None,
        "carrier_offset_from_target_hz":
            float(center_freq + fo - target_freq) if (center_freq and target_freq) else None,
        "snr_db": carrier["snr_db"],
        "peak_db": carrier["power_db"],
        "noise_floor_db": float(np.median(psd_db)),
        "bw_3db_hz": bw3,
        "bw_20db_hz": bw20,
        "on_fraction": float(on.mean()),
        "keying_100ms": keying,
        "drift_hz_per_s": drift["slope_hz_per_s"],
        "drift_span_hz": drift["span_hz"],
        "drift": drift,
        "top_peaks": peaks,
    }
    return report


def format_carrier_report(r):
    """Format an analyze_carrier() dict as human-readable text."""
    lines = [
        "Carrier Detection",
        "=" * 44,
        f"Duration:          {r['duration_s']:.2f} s",
        f"Sample rate:       {r['sample_rate_hz']:,} Hz",
    ]
    if r["center_freq_hz"]:
        lines.append(f"SDR center:        {r['center_freq_hz']/1e6:.4f} MHz")
    if r["target_freq_hz"]:
        lines.append(f"Target uplink:     {r['target_freq_hz']/1e6:.4f} MHz")
    lines += [
        "",
        "Strongest carrier",
        "-" * 44,
        f"Offset (baseband): {r['carrier_offset_hz']/1e3:+.1f} kHz",
    ]
    if r["carrier_freq_hz"]:
        lines.append(f"Absolute freq:     {r['carrier_freq_hz']/1e6:.4f} MHz")
    if r["carrier_offset_from_target_hz"] is not None:
        lines.append(f"Offset vs target:  {r['carrier_offset_from_target_hz']/1e3:+.1f} kHz")
    lines += [
        f"SNR over floor:    {r['snr_db']:.1f} dB",
        f"-3 dB width:       {r['bw_3db_hz']/1e3:.2f} kHz",
        f"-20 dB width:      {r['bw_20db_hz']/1e3:.2f} kHz",
        f"On fraction:       {r['on_fraction']*100:.0f}%",
        f"Keying (100 ms):   {r['keying_100ms']}",
        f"Doppler drift:     {r['drift_hz_per_s']:+.0f} Hz/s ({r['drift_span_hz']:+.0f} Hz across carrier-present windows)",
        "",
        "Top peaks (offset : SNR over floor)",
        "-" * 44,
    ]
    for p in r["top_peaks"]:
        lines.append(f"  {p['offset_hz']/1e3:+9.1f} kHz   {p['snr_db']:+5.1f} dB")
    return "\n".join(lines)


# --- IQ to audio -----------------------------------------------------------
# The demodulation happens on board in capture.cpp; these helpers reproduce it
# on the ground so raw IQ downlinks can be turned into audio. FM mode mirrors
# the flight chain (channel low-pass, quadrature demod, resample, voice
# band-pass). CW mode mixes a carrier to an audible beat tone so a keyed
# transmission can be heard.

def mix_baseband(iq, sample_rate, offset_hz):
    """Shift a signal at offset_hz down to 0 Hz (baseband)."""
    if not offset_hz:
        return iq
    t = np.arange(len(iq)) / sample_rate
    return iq * np.exp(-1j * 2 * np.pi * offset_hz * t)


def channelize(iq, sample_rate, bw_hz, decim=1):
    """Low-pass to +/- bw_hz/2 and optionally decimate. Returns (iq, new_rate)."""
    taps = signal.firwin(255, min((bw_hz / 2) / (sample_rate / 2), 0.99))
    filtered = signal.lfilter(taps, 1, iq)
    if decim > 1:
        filtered = filtered[::decim]
        sample_rate = sample_rate / decim
    return filtered, sample_rate


def fm_demodulate(iq, sample_rate, deviation_hz):
    """FM discriminator matching the on-board quadrature_demod_cf: the phase
    difference between adjacent samples, scaled by sample_rate/(2*pi*deviation)."""
    d = iq[1:] * np.conj(iq[:-1])
    return np.angle(d) * (sample_rate / (2 * np.pi * deviation_hz))


def ssb_demodulate(iq, sample_rate, sideband="auto", voice_band=(300.0, 3400.0)):
    """Single-sideband product detector for a signal already mixed to baseband
    (suppressed carrier at 0 Hz). Keeps one sideband with a frequency-domain
    mask and returns the real audio, correctly oriented.

    sideband: 'lsb', 'usb', or 'auto' (pick the sideband with more energy in
    the voice band). Returns (audio_real, sideband_used).
    """
    n = len(iq)
    spec = np.fft.fft(iq)
    freqs = np.fft.fftfreq(n, 1.0 / sample_rate)
    lo, hi = voice_band
    if sideband == "auto":
        e_lsb = np.abs(spec[(freqs <= -lo) & (freqs >= -hi)]).sum()
        e_usb = np.abs(spec[(freqs >= lo) & (freqs <= hi)]).sum()
        sideband = "lsb" if e_lsb >= e_usb else "usb"
    mask = np.zeros(n)
    if sideband == "lsb":
        mask[freqs < 0] = 1.0
    else:
        mask[freqs > 0] = 1.0
    one = np.fft.ifft(spec * mask)
    return 2.0 * one.real, sideband


def resample_to(x, sr_in, sr_out):
    """Rational resample real audio from sr_in to sr_out."""
    from math import gcd
    g = gcd(int(round(sr_in)), int(round(sr_out)))
    return signal.resample_poly(x, int(round(sr_out)) // g, int(round(sr_in)) // g)


def bandpass(x, sample_rate, low_hz, high_hz, num_taps=255):
    """Voice band-pass, matching the on-board 300-3400 Hz stage."""
    taps = signal.firwin(num_taps, [low_hz, high_hz], fs=sample_rate, pass_zero=False)
    return signal.lfilter(taps, 1, x)


def write_wav_mono(path, samples, sample_rate, peak_normalize=True):
    """Write float samples to a 16-bit mono WAV, peak-normalized by default."""
    s = np.asarray(samples, dtype=np.float32)
    if peak_normalize:
        s = s / (np.max(np.abs(s)) + 1e-12)
    pcm = np.int16(np.clip(s, -1.0, 1.0) * 32767)
    wavfile.write(path, int(round(sample_rate)), pcm)
