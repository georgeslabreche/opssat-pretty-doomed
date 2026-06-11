# Run 3: 2026-06-09

- **Pack:** `pack-4023_1781041655`
- **Captures:** 6 x 20 s
- **Spacecraft:** OPS-SAT PRETTY, NORAD 58023
- **Target:** Legnica, Poland
- **Scheduled time (app start):** 2026-06-09T21:42:49Z
- **Target ECEF (m):** 3845782.0, 1114412.25, 4948097.5 (commanded by the ADCS)

## Pre-conditions

- Attitude: commanded to track Legnica; pointing is initiated before AOS and runs on its own schedule. The scheduled time marks the app start (the log opens 4 s after it, startup latency), and the six captures ran from +10 s to +141 s after it. During the captures the +X antenna was 3-11 deg off Legnica, closing to 2.7 deg at the end of capture 6. The angle was still decreasing at the last telemetry sample (0.56 deg at +301 s), so the track minimum lies at or beyond +301 s.
- Broadcast: confirmed by the operator. The exact on-air window was not logged.

## Result

No detection. All six captures returned `total_points=0`; transcriptions are the same degenerate "I" / "AND I" pattern as Runs 1 and 2. Audio post-DSP is noise only on listening, with no audible voice.

The receiver-side numbers diverge from the established Run 1 + Run 2 baseline:

| Capture | +X to Legnica (mean) | RMS I (dBFS) | Peak I | PAPR I (dB) | Zero fraction |
|---|---|---|---|---|---|
| 1 | 10.7 deg | -48.2 | 6217  | 45.8 | 2.17 % |
| 2 | 8.9 deg  | -46.2 | 9245  | 47.3 | 2.30 % |
| 3 | 6.0 deg  | -44.8 | 10110 | 46.6 | 2.46 % |
| 4 | 8.4 deg  | -46.0 | 9849  | 47.6 | 2.49 % |
| 5 | 6.0 deg  | -45.1 | 9208  | 46.1 | 2.56 % |
| 6 | 4.6 deg  | -46.6 | 9430  | 47.9 | 2.72 % |

Run 1 + 2 baseline was RMS -52.5 to -52.7 dBFS, Peak 370-1358, PAPR 26-37 dB; Run 3 sits 4-8 dB above that floor with peaks near 10000 and PAPR 45-48 dB, and RMS varies 3+ dB across captures (vs 0.1 dB on Runs 1 and 2). The per-capture PSD CSVs put the lift at roughly 4-9 dB across the full passband, flat: in every capture the median within ±10 kHz of DC matches the 20-70 kHz median to within 0.2 dB, so there is no concentration at the FM voice bandwidth. Of the stable internal narrowband features Runs 1 and 2 show (a -14 kHz spur 4-5 dB up, weaker ±5.1 kHz and +18 kHz lines ~2 dB up), the -14 kHz spur is largely buried and the +18 kHz line persists; new per-capture features appear at +5.1 kHz (capture 1), +16.4 kHz (capture 5), and +14.3 kHz (capture 6). None follow the Doppler track a Legnica transmitter would have (+14.6 kHz falling through zero to -20 kHz across the capture window).

We have a broadcast confirmed by the operator with an unlogged on-air window, an antenna 3-11 deg off the operator during all six captures, and elevated RF energy at the ADC with a strongly impulsive character (PAPR 45-48 dB). Audio listening rules out voice content above threshold.

## Pipeline timing

- AD9361 config: 5.7 s.
- STT model load: 23 s.
- Capture wall time: 21-23 s per capture, for exactly 20.0 s of samples each.
- STT inference: 37-52 s per capture.
- Total run time: 282.3 s.
- Clean shutdown, no errors.

## Files

The on-board per-capture artifacts under `captures/` follow the naming pattern `capture-NNN-{metrics,psd}.csv`, `capture-NNN-{scores,summary,transcription}.txt`, and `capture-NNN-{spectrogram,psd}.png`. The cross-capture PSD overlay produced on board is at `psd-comparison.png`.
