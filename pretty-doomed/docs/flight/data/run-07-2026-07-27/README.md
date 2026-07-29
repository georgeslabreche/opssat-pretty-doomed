# Run 7: 2026-07-27 (evening pass)

- **Pack:** `pack-4023_1785188432` (full downlinked archive committed as [`pack-4023_1785188432.tar.gz`](pack-4023_1785188432.tar.gz))
- **Captures:** 6 x 20 s configured; 4 completed, 1 partial, 1 empty (see Result)
- **Spacecraft:** OPS-SAT PRETTY, NORAD 58023
- **Target:** Oslo, Norway (scheduled); not commanded (ADCS reset), actual orientation unknown, see Attitude
- **Broadcast:** live human voice over a microphone (no TTS keyer), same message structure as the keyer with the DOOM keyword repeated at the end, transmitted by the Oslo team
- **Scheduled time (app start):** 2026-07-27T21:36:56Z
- **Software:** v7 (narrowing armed, `doom_force_trigger=false`, hardware FIR)

## Result

Detection of the voice command on a pass with the ADCS reset (attitude unknown). Capture 2 detected and launched DOOM on an exact `DOOM, DOOM` command match:

| Capture | RMS I (dBFS) | Peak I | Zero frac | Result | Demo |
|---|---|---|---|---|---|
| 1 | -48.5 | 2571 | 1.85 % | no | |
| 2 | -48.3 | 3891 | 1.77 % | **DETECTED** | gl-e1m2b |
| 3 | -52.4 | 3637 | 2.33 % | no | |
| 4 | -49.9 | 5045 | 1.81 % | no | |
| 5 | -52.4 | 502 | 1.96 % | no (partial capture) | |
| 6 | -- | -- | -- | no (empty capture) | |

Capture 2 transcript: `AND FAR BRIEFLY PLAYING DOOM STEAM DOOM PRECIPATED`. Received levels are at the noise floor, comparable to Run 6's misses; the detection landed where a clean `DOOM DOOM` fell inside the 20 s window.

Two operational anomalies, reported as separate observations:

- **ADCS reset.** Operations reported an ADCS reset; the spacecraft was not commanded to track the target and no attitude telemetry is available, so the actual antenna orientation during the pass is unknown. A detection still occurred, but with the attitude unknown this is not evidence that pointing is unnecessary.
- **SDR sample delivery stalled.** Capture 5 stopped receiving samples at 808,277 of 4,000,000 (about 20 %) and hit the 50 s timeout, logged as "SDR capture partial"; capture 6 returned an empty file (header only). The pipeline handled both gracefully: partial capture flagged, STT skipped on the too-short audio, clean shutdown, no crash. This is an SDR streaming stall, distinct from the ADCS reset.

## Pipeline timing

- STT model load: 21.4 s.
- Total run time: 212.8 s (short because captures 5 and 6 produced little or no data).

## Files

Per-capture on-board artifacts under `captures/` follow the same naming as Run 6 (`capture-NNN-{metrics,psd}.csv`, `capture-NNN-{scores,summary,transcription}.txt`, `capture-NNN-{spectrogram,psd}.png`), for the captures that produced them. The cross-capture PSD overlay is `psd-comparison.png`. No `ukf-attitude.csv`: attitude telemetry does not exist for this pass. The complete downlinked archive is [`pack-4023_1785188432.tar.gz`](pack-4023_1785188432.tar.gz) (SHA-256 `c2a3217acecccbd78b24ac89995c0af7002a878ee3f6be49ed8b99ea9ae1814d`). Analysis is in the [debriefing](../../debriefings/run-07-2026-07-27/), which refers to [Run 6](../../debriefings/run-06-2026-07-27/) for the shared signal-processing analysis.
