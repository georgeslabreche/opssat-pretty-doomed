#!/usr/bin/env python3
"""
Simulate the radio trip: clean voice WAV -> flight-form baseband I/Q (sc16).

The output is what the onboard pipeline records as capture.sc16 (complex
baseband at the 200 kHz effective rate, interleaved little-endian int16), so
it feeds the actual flight binary via `pretty-doomed -r` (issue #116) with the
narrowing, peak search, and FM discriminator all inside the test loop. This is
the instrument for screening keyer candidates at a chosen link quality before
they are transmitted.

Chain: normalize audio -> upsample to the output rate -> FM modulate at
--deviation -> shift to --offset-hz with optional linear --drift-hz-s -> add
noise to hit --cnr-db -> rescale -> write sc16.

CNR definition: carrier power over the noise power falling inside --cnr-bw
(default 20 kHz, the narrowing bandwidth, i.e. what actually reaches the
discriminator). Stated CNR values are meaningless without their bandwidth;
this one is chosen so "CNR X dB" describes the discriminator's own input.

Noise source: complex AWGN by default, or --noise-file, a noise-only sc16 at
the output rate (e.g. a keying pause cut from a real flight capture and
converted with make_emu_replay.py --out-rate 200000), tiled to length and
scaled to the target CNR. The AWGN default matches the measured flatness of
the Run 5 noise floor but omits its narrowband features (spurs, DC spike);
real capture noise carries them.

Defaults: deviation 5 kHz (the flight demodulator's configured
sdr_fm_deviation, consistent with the received Run 5 signal fitting the
validated +/-10 kHz narrowing), uplink at -8 kHz from DC (the Run 5 offset
measured at zenith).

Usage:
  python3 make_fm_iq.py voice.wav --output voice_cnr12.sc16 --cnr-db 12
  python3 make_fm_iq.py voice.wav --output voice_clean.sc16          # no noise
"""
import argparse
import sys

import numpy as np

from iq import read_wav_mono, read_sc16, resample_to


def fm_modulate(audio, deviation_hz, sample_rate):
    """Unit-amplitude FM: instantaneous frequency = deviation * audio (audio in [-1, 1])."""
    phase = 2.0 * np.pi * deviation_hz * np.cumsum(audio) / sample_rate
    return np.exp(1j * phase)


def band_noise_fraction(bw_hz, sample_rate, noise=None, center_hz=0.0):
    """Fraction of the noise power that falls inside +/-bw/2 of center_hz.

    For white noise this is bw/fs regardless of center; for a real noise
    recording (spurs, DC spike, shaped floor) it is measured from the
    spectrum around the carrier placement, so --cnr-db states the CNR in the
    band the narrowing stage actually delivers to the discriminator."""
    if noise is None:
        return min(1.0, bw_hz / sample_rate)
    spec = np.abs(np.fft.fft(noise)) ** 2
    freqs = np.fft.fftfreq(len(noise), 1.0 / sample_rate)
    return max(1e-6, float(spec[np.abs(freqs - center_hz) <= bw_hz / 2.0].sum() / spec.sum()))


def main():
    p = argparse.ArgumentParser(description="Clean voice WAV -> flight-form FM baseband sc16")
    p.add_argument("input", help="Clean voice WAV (any rate, mono or stereo)")
    p.add_argument("--output", required=True, help="Output sc16 file")
    p.add_argument("--out-rate", type=int, default=200000, help="Output sample rate in Hz, the flight effective rate (default: 200000)")
    p.add_argument("--deviation", type=float, default=5000.0, help="Peak FM deviation in Hz (default: 5000, the flight demodulator's configured deviation)")
    p.add_argument("--offset-hz", type=float, default=-8000.0, help="Carrier offset from DC in Hz (default: -8000, the Run 5 zenith offset)")
    p.add_argument("--drift-hz-s", type=float, default=0.0, help="Linear Doppler drift in Hz/s (default: 0)")
    p.add_argument("--cnr-db", type=float, default=None, help="Carrier-to-noise ratio in dB within --cnr-bw; omit for no noise")
    p.add_argument("--cnr-bw", type=float, default=20000.0, help="CNR reference bandwidth in Hz (default: 20000, the narrowing bandwidth)")
    p.add_argument("--noise-file", help="Noise-only sc16 at --out-rate to use instead of AWGN, tiled and scaled to --cnr-db")
    p.add_argument("--pad-s", type=float, default=0.25, help="Noise-only padding before and after the voice in seconds (default: 0.25)")
    p.add_argument("--peak", type=float, default=2000.0, help="Peak int16 amplitude of the output (default: 2000)")
    p.add_argument("--seed", type=int, default=4023, help="RNG seed for reproducible noise (default: 4023)")
    args = p.parse_args()

    fs = args.out_rate
    audio, in_rate = read_wav_mono(args.input)
    audio = audio / (np.max(np.abs(audio)) + 1e-12)
    audio = resample_to(audio, in_rate, fs)

    signal = fm_modulate(audio, args.deviation, fs)

    # Padding: carrier off before and after the transmission, noise only.
    n_pad = int(round(args.pad_s * fs))
    signal = np.concatenate([np.zeros(n_pad, dtype=complex), signal, np.zeros(n_pad, dtype=complex)])

    t = np.arange(len(signal)) / fs
    signal *= np.exp(2j * np.pi * (args.offset_hz * t + 0.5 * args.drift_hz_s * t * t))

    if args.cnr_db is not None:
        # Carrier power is 1 (unit-amplitude FM). Total noise power such that
        # the fraction inside the reference bandwidth gives the requested CNR.
        if args.noise_file:
            noise = read_sc16(args.noise_file)
            reps = int(np.ceil(len(signal) / len(noise)))
            noise = np.tile(noise, reps)[: len(signal)]
            p_noise_total = 10.0 ** (-args.cnr_db / 10.0) / band_noise_fraction(args.cnr_bw, fs, noise, args.offset_hz)
            noise = noise * np.sqrt(p_noise_total / (np.mean(np.abs(noise) ** 2) + 1e-30))
        else:
            p_noise_total = 10.0 ** (-args.cnr_db / 10.0) / band_noise_fraction(args.cnr_bw, fs)
            rng = np.random.default_rng(args.seed)
            noise = (rng.standard_normal(len(signal)) + 1j * rng.standard_normal(len(signal))) \
                * np.sqrt(p_noise_total / 2.0)
        signal = signal + noise

    out = signal * (args.peak / (np.max(np.abs(np.concatenate([signal.real, signal.imag]))) + 1e-12))
    interleaved = np.empty(2 * len(out), dtype="<i2")
    interleaved[0::2] = np.round(out.real).astype("<i2")
    interleaved[1::2] = np.round(out.imag).astype("<i2")
    interleaved.tofile(args.output)

    cnr = "none" if args.cnr_db is None else f"{args.cnr_db:g} dB in {args.cnr_bw / 1e3:g} kHz"
    print(f"wrote {args.output}: {len(out) / fs:.2f}s at {fs} Hz, "
          f"deviation {args.deviation / 1e3:g} kHz, carrier at {args.offset_hz / 1e3:+g} kHz"
          f"{f' drifting {args.drift_hz_s:+g} Hz/s' if args.drift_hz_s else ''}, "
          f"CNR {cnr}, noise {'file' if args.noise_file else 'AWGN'}, "
          f"peak {int(np.max(np.abs(interleaved)))}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
