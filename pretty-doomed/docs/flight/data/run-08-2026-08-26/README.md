# Run 8: 2026-08-26 (evening pass)

- **Pack:** `pack-4023_1787780178`
- **Captures:** 6 x 20 s configured, 4 complete, 1 partial, 1 empty
- **Spacecraft:** OPS-SAT PRETTY, NORAD 58023
- **Target:** Oslo, Norway (from the ECEF target supplied by operations, `3149143.1,598008.8,5495694.5`)
- **Scheduled time (app start):** 2026-08-26T21:32:44Z
- **Software:** v8 (detection-conditional sc16 retention, narrowing armed, `doom_force_trigger=false`, hardware FIR)

The pack also carries a run from 2026-08-19. TUG confirmed that one was a verification run to check the v8 update had installed and could execute on board, left in place afterwards, so it appears in the same downlink. It is not a flight attempt and is not part of this record.

## Result

First coordinated pass with v8 flying, and no detection. Operations reported that the experiment was started with an incorrect script, which pointed the spacecraft at the wrong location during the acquisition. The pointing audit confirms it: the +X antenna boresight error to Oslo was 88.85 deg at app start, essentially perpendicular to the line of sight, on a pass whose geometry was otherwise excellent.

| Capture | Wall | RMS I (dBFS) | Peak I | Zero frac I | Narrowing peak | Transcript | sc16 |
|---|---|---|---|---|---|---|---|
| 1 | 20 s | -46.46 | 10391 | 2.40 % | +16.02 kHz | `AND SO` | deleted |
| 2 | 21 s | -46.79 | 8993 | 2.25 % | +14.65 kHz | `TO HIM` | deleted |
| 3 | 21 s | -47.82 | 9009 | 2.28 % | +12.72 kHz | `AND SOULS` | deleted |
| 4 | 21 s | -46.22 | 10125 | 2.37 % | +13.04 kHz | `TOO` | deleted |
| 5 | 50 s | -46.58 | 8523 | 2.03 % | +12.79 kHz | `AMIS` (partial capture) | deleted |
| 6 | 50 s | -- | -- | -- | narrowing failed | none (empty capture) | deleted |

Every transcript is a short noise fragment of the kind the recogniser produces on static. Received level runs a few dB hotter than [Run 7](../run-07-2026-07-27/) at -46 to -48 dBFS against -48 to -52 dBFS there.

## Attitude

`ukf-attitude.csv` holds 31 UKF quaternion samples at 10 s cadence from 21:30:00Z to 21:34:50Z, covering -164 s to +126 s about app start, so captures 1 to 4 are fully covered, capture 5 partially, and capture 6 not at all.

The +X boresight error to Oslo, computed from the quaternions and a TLE of epoch 2026-09-02 (7 days after the pass), rises from 23 deg at -165 s to a maximum of 88.85 deg at app start, then falls steadily to 27 deg by the end of telemetry. The error was therefore at its worst exactly when the captures began. Spacecraft elevation from Oslo was 43 deg at app start, peaking near 80 deg during capture 3, at a slant range of 706 km. Analysis and figures in the [debriefing](../../debriefings/run-08-2026-08-26/).

## SDR sample stall

Captures 1 to 4 delivered full sample counts at about 21 s wall. Capture 5 stopped receiving at 1,179,648 of 4,000,000 samples, roughly 5.9 s of data, and was held to the 50 s timeout and logged as `SDR capture partial`. Capture 6 received nothing at all and its sc16 was written header-only. The signature matches [Run 7](../../debriefings/run-07-2026-07-27/), which lost the same two capture indices. The pipeline degraded gracefully with no crash, as it did there.

## SC16 retention

First coordinated-pass flight of `sdr_keep_sc16=detected`. With no detection, the policy deleted all six sc16 files after their artifact consumers finished, each with a logged decision, including capture 5's partial file and capture 6's header-only file. No `capture.sc16` survives in the pack. The keep branch fires only on a detection and has still not run on orbit.

## Pipeline timing

- STT model load: 23.7 s (in parallel with capture 1).
- Total run time: 213.7 s, shortened by the two timed-out captures skipping their normal processing load. Clean shutdown.

## Files

Per-capture on-board artifacts under `captures/` follow `capture-NNN-{metrics,psd}.csv`, `capture-NNN-{scores,summary,transcription}.txt`, and `capture-NNN-{spectrogram,psd}.png` (spectrogram and PSD converted from the on-board BMPs). Capture 6 has no metrics, PSD, or spectrogram, the empty capture having produced nothing to measure. The cross-capture PSD overlay is `psd-comparison.png` and the attitude telemetry is `ukf-attitude.csv`. The raw I/Q does not exist for this run by design, having been deleted on board by the retention policy.
