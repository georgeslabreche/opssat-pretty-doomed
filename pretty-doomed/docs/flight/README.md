# OPS-SAT PRETTY voice-uplink: flight runs

A small data drop per flight attempt of the PRETTY DOOMed experiment, kept here so the receiver-side record is reproducible without external storage.

## Runs so far

| Run | Date (UTC) | Pack | Attitude | Broadcast | Result |
|---|---|---|---|---|---|
| [01](data/run-01-2026-04-21/) | 2026-04-21 20:14 | `pack-4023_1776802706` | confirmed bad pointing | confirmed transmitted | no detection |
| [02](data/run-02-2026-05-22/) | 2026-05-22 21:51 | `pack-4023_1779486954` | good pointing, +X antenna ~5 deg on target | did not happen | no detection |

## Per-run layout

Each `data/run-NN-YYYY-MM-DD/` directory contains:

- `config.cfg`: onboard experiment config that ran.
- `pretty-doomed.log`: experiment script log.
- `resource.csv`: per-second CPU, memory, and timing telemetry.
- `ukf-attitude.csv`: UKF quaternion telemetry across the capture window. Consumed by the pointing scripts in `scripts/attitude/`.
- `psd-comparison.png`: cross-capture PSD overlay generated on board.
- `captures/capture-NNN-metrics.csv` and `capture-NNN-psd.csv`: per-capture audio quality and PSD numbers.
- `captures/capture-NNN-scores.txt`, `capture-NNN-summary.txt`, `capture-NNN-transcription.txt`: detection scores, capture summary, and the speech-to-text output.
- `captures/capture-NNN-spectrogram.png` and `capture-NNN-psd.png`: per-capture spectrogram and PSD plot generated on board.

The raw I/Q, the WAV audio, and the constellation BMPs are left in the gitignored `artifacts/pretty/` packs; the receiver-side story is fully covered by the files above.

## Debriefings

Post-run debriefings are written in markdown under `debriefings/YYYY-MM-DD/`, where the date is the UTC date of the flight being debriefed. Each debriefing is self-contained: its README is the debriefing itself, and any figures live alongside it.

- [`debriefings/2026-05-22/`](debriefings/2026-05-22/): first two flight runs (Run 1 on 2026-04-21 as negative control, Run 2 on 2026-05-22 as the most recent attempt), with the verified pointing analysis.

The longer prose write-up of the flight runs lives at `artifacts/pretty/FLIGHT_ANALYSIS.md` and is not committed to the repo.
