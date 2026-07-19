#!/usr/bin/env python3
"""Denoise pretty-doomed capture.wav files with noisereduce spectral gating.

Finds every capture.wav under the source directory (a single run directory or
a parent containing many) and writes a denoised 16-bit WAV per capture into
the destination directory, named after the run directory it came from. The
output feeds back into the flight pipeline at the STT stage via
`pretty-doomed -i`, which is how the detection-level A/B in README.md re-runs
the identical pipeline with the denoiser as the only difference.

Usage:
  python3 denoise.py <src_dir> <dst_dir>
"""
import glob
import os
import sys

import numpy as np
from scipy.io import wavfile
import noisereduce as nr


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 1
    src_root, dst_root = sys.argv[1], sys.argv[2]
    os.makedirs(dst_root, exist_ok=True)
    wavs = sorted(glob.glob(f'{src_root}/**/capture.wav', recursive=True))
    if not wavs:
        print(f'no capture.wav found under {src_root}')
        return 1
    for wav in wavs:
        run_dir = os.path.dirname(os.path.dirname(wav))
        name = os.path.basename(run_dir.rstrip('/')) or 'capture'
        rate, data = wavfile.read(wav)
        audio = data.astype(np.float32) / 32768.0
        denoised = nr.reduce_noise(y=audio, sr=rate, stationary=True)
        out = np.clip(denoised, -1.0, 1.0)
        wavfile.write(f'{dst_root}/{name}.wav', rate, (out * 32767).astype(np.int16))
        print(f'denoised {name} ({len(audio) / rate:.1f}s at {rate} Hz)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
