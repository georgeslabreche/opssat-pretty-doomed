# Run 4: 2026-06-10

- **Pack:** `pack-4023_1781126762`
- **Captures:** 6 x 20 s
- **Spacecraft:** OPS-SAT PRETTY, NORAD 58023
- **Target:** Legnica, Poland
- **Scheduled time (app start):** 2026-06-10T21:21:20Z

## Pre-conditions

- Attitude: commanded to track Legnica, and it did. The UKF telemetry shows the +X antenna about 13 deg off in capture 1, closing to 1.8 deg by the end of capture 6 and about 0.4 deg at the last telemetry sample (+268 s), on-axis for the patch antenna throughout (per-capture means 13.1, 10.1, 5.9, 7.2, 7.9, 2.7 deg). The spacecraft pointed at Legnica; the link did not close because the ground station could not point back. Pass geometry from the TLE (epoch 2026-06-10 11:42 UTC): a high-elevation pass with elevation from Legnica peaking at 73 deg at +62 s, during capture 3, minimum slant range 521 km.
- Broadcast: attempted, but the ground station probably could not keep the beam on the spacecraft. It ran upgraded hardware (100 W, directional antenna with a ~10 deg beamwidth), and the antenna mount could not tilt far enough to track this high-elevation pass. The operator's read is that they could not get the elevation to reach the satellite and expects nothing in the recording. Whether the beam reached the spacecraft cannot be confirmed from the receiver side.

## Result

No detection. All six captures returned `total_points=0`; transcriptions are the degenerate "I" / "OH" pattern. The lifted, impulsive noise floor first seen in Run 3 reproduced:

| Capture | RMS I (dBFS) | Peak I | PAPR I (dB) | Zero fraction |
|---|---|---|---|---|
| 1 | -46.0 | 9880 | 47.7 | 2.63 % |
| 2 | -46.4 | 8065 | 46.2 | 2.55 % |
| 3 | -46.3 | 8453 | 46.6 | 2.51 % |
| 4 | -44.6 | 9773 | 46.1 | 2.42 % |
| 5 | -44.4 | 9598 | 45.8 | 2.46 % |
| 6 | -45.1 | 8585 | 45.5 | 2.48 % |

Against the Run 1 + 2 baseline (RMS -52.5 to -52.7 dBFS, peaks 370-1358, PAPR 26-37 dB): RMS sits 6-8 dB above, peaks near 10000, PAPR 45-48 dB, with ~2 dB RMS variation across captures. The per-capture PSD CSVs put the broadband lift at 4-9 dB (inner ±10 kHz band -106.8 to -109.0 dBFS, 20-70 kHz band -106.2 to -108.4 dBFS).

New in this run: the spectrum is asymmetric. A broad hump spans roughly -52 to -78 kHz, 3-5 dB above the positive side of the band (Run 3 was symmetric to within 0.2 dB). It is strongest in captures 1-3 and fades through captures 4-6, and it stays at a fixed frequency rather than sweeping, so it is not a Doppler-shifted carrier. The +18 kHz internal line remains visible in most captures; spectrogram streak columns (more than 5 MAD above the median) run 28-40 of 1024 versus the 2-14 baseline.

## Pipeline timing

- AD9361 config: 6.3 s.
- STT model load: 23.2 s (in parallel with capture 1).
- Capture wall time: 21.6-23.4 s per capture, for exactly 20.0 s of samples each.
- STT inference: 37.5-48.1 s per capture.
- Total run time: 278.8 s.
- Clean shutdown, no errors.

## Files

The on-board per-capture artifacts under `captures/` follow the naming pattern `capture-NNN-{metrics,psd}.csv`, `capture-NNN-{scores,summary,transcription}.txt`, and `capture-NNN-{spectrogram,psd}.png`. The cross-capture PSD overlay produced on board is at `psd-comparison.png`. The UKF attitude telemetry is at `ukf-attitude.csv`, consumed by the pointing scripts in `scripts/attitude/`.
