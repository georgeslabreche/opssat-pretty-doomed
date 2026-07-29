# Run 6: 2026-07-27 (morning pass)

- **Pack:** `pack-4023_1785149761` (full downlinked archive committed as [`pack-4023_1785149761.tar.gz`](pack-4023_1785149761.tar.gz))
- **Captures:** 6 x 20 s
- **Spacecraft:** OPS-SAT PRETTY, NORAD 58023
- **Target:** Oslo, Norway (59.9139 N, 10.7522 E, 23 m; from the ECEF target supplied by operations)
- **Scheduled time (app start):** 2026-07-27T10:50:53Z
- **Software:** v7 (narrowing armed, `doom_force_trigger=false`, hardware FIR)

## Result

First on-orbit detection of the voice command. Three of the six captures detected and launched DOOM (`doom_force_trigger=false`, so these are genuine detections):

| Capture | Window (past 10:50:00) | RMS I (dBFS) | Peak I | Zero frac | Result | Demo |
|---|---|---|---|---|---|---|
| 1 | 62-83 s | -44.6 | 6941 | 0.85 % | **DETECTED** | gl-e1m2b |
| 2 | 87-109 s | -50.8 | 7945 | 2.69 % | no | |
| 3 | 113-134 s | -53.2 | 6343 | 3.34 % | no | |
| 4 | 139-166 s | -46.6 | 5032 | 0.97 % | **DETECTED** | e1m7-607 |
| 5 | 171-199 s | -48.4 | 6067 | 1.20 % | **DETECTED** | gl-e1m2 |
| 6 | 203-225 s | -49.6 | 3980 | 2.45 % | no | |

The keyer transmission is legible in the transcripts of the detected captures, e.g. capture 1: `... PRETTY PRETTY PLEASE PLAY DOON DO ... THE FORASCAR PRETTY PRETTY PLEASE PLAY DOOMS ... AFORE OSSICA PRETTY PRETTY PLEASE PLAY ...` (the looping message, with call-sign fragments `FORASCAR` = FOUR OSCAR, `OSSICA` = OSCAR). All three detections were on fuzzy command matches (`DOON` to `DOOM` at distance 1); the scores files hold the exact per-capture counts.

Detection tracks per-capture received level, not the pointing state: the three detected captures are the three with the highest RMS I (-44.6, -46.6, -48.4 dBFS) and the lowest zero fraction, and the two clearest misses (captures 2, 3) are the weakest captures (-50.8, -53.2 dBFS). Received level overall is at the noise floor in wideband terms; the narrowing stage recovers the voice from the band around the uplink.

## Attitude

`ukf-attitude.csv` holds 33 UKF quaternion samples at 10 s cadence from 10:50:10Z. The +X antenna boresight error to Oslo, computed from the quaternions and a TLE (epoch 2026-07-29, about 2.2 days after the pass; propagation caveat in the debriefing), is 15-19 deg through captures 1-3, reaches a minimum near 9 deg during capture 4, rises to about 24 deg across captures 5-6, and settles near 10 deg afterward. The rise across captures 5-6 is the post-zenith deviation operations reported (around 10:53:20 +/- 40 s). Spacecraft elevation from Oslo was about 40 deg at app start, at a slant range near 740 km.

Detection does not follow pointing error alone: capture 4 detected at best pointing and capture 6 missed at worst pointing, but capture 3 missed at good pointing (~12 deg) while capture 1 detected at ~16 deg. Detection tracks the per-capture received level (RMS I above), which is shaped by both the antenna pointing and where the looping keyer's clean command fell in the 20 s window. Full pointing analysis and plot in the [debriefing](../../debriefings/run-06-2026-07-27/).

## Pipeline timing

- STT model load: 24.5 s (in parallel with capture 1).
- Capture wall time: 20-21 s per capture, for 20.0 s of samples each.
- Total run time: 304.1 s. Clean shutdown, service exit `status=127` as in all packs.

## Files

Per-capture on-board artifacts under `captures/` follow `capture-NNN-{metrics,psd}.csv`, `capture-NNN-{scores,summary,transcription}.txt`, and `capture-NNN-{spectrogram,psd}.png` (spectrogram/PSD converted from the on-board BMPs). The cross-capture PSD overlay is `psd-comparison.png`; UKF attitude telemetry is `ukf-attitude.csv`. The complete downlinked archive, including the WAV audio, constellations, and the DOOM demo outputs and postcards, is [`pack-4023_1785149761.tar.gz`](pack-4023_1785149761.tar.gz) (SHA-256 `3fb4852f0422f2d2c995573a489aae4bf2b6f26ee8dac61615c7cca0306a6665`). Analysis and the milestone write-up are in the [debriefing](../../debriefings/run-06-2026-07-27/).
