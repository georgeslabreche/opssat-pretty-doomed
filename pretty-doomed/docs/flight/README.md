# OPS-SAT PRETTY voice-uplink: flight runs

A small data drop per flight attempt of the PRETTY DOOMed experiment, kept here so the receiver-side record is reproducible without external storage.

## Runs so far

| Run | Date (UTC) | Pack | Attitude | Broadcast | Result |
|---|---|---|---|---|---|
| [01](data/run-01-2026-04-21/) | 2026-04-21 20:14 | `pack-4023_1776802706` | confirmed bad pointing | confirmed transmitted | no detection |
| [02](data/run-02-2026-05-22/) | 2026-05-22 21:51 | `pack-4023_1779486954` | good pointing, +X antenna ~5 deg on target | did not happen | no detection |
| [03](data/run-03-2026-06-09/) | 2026-06-09 21:42 | `pack-4023_1781041655` | near-target pointing, +X antenna 3-11 deg off in captures | confirmed transmitted | no detection, noise floor lifted 4-9 dB with high PAPR |
| [04](data/run-04-2026-06-10/) | 2026-06-10 21:21 | `pack-4023_1781126762` | good pointing, +X antenna 2-14 deg off on a high-elevation pass | attempted, ground antenna likely could not track the pass | no detection, lifted impulsive floor reproduced on a second June pass |
| [05](data/run-05-2026-07-03/) | 2026-07-03 19:25 | RF-link test, wideband IQ | not applicable, RF-link test | carrier from an Oslo radio-amateur team | signal detected in 5 of 6 snapshots, +23 to +26 dB over floor; externally received per Doppler drift; carries voice, not the FM the pipeline expects |

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

The raw I/Q, the WAV audio, and the constellation BMPs are too large for the repo and are not included; the receiver-side story is fully covered by the files above.

Run 5 is an RF-link test rather than a Doom-pipeline run, so `data/run-05-2026-07-03/` instead holds the ground-side carrier-detection outputs (`carrier.txt`, `carrier.json`, `spectrogram.svg`, `carrier.svg`, `carrier_drift.svg`, `psd.svg`), a demodulated `audio-cw.wav`, and `rf_test.zip` with all six snapshots demodulated several ways. Its raw wideband IQ is not included. See its README.

## Debriefings

Post-run debriefings are written in markdown under `debriefings/YYYY-MM-DD/`, where the date is the UTC date of the flight being debriefed. Each debriefing is self-contained: its README is the debriefing itself, and any figures live alongside it.

- [`debriefings/2026-05-22/`](debriefings/2026-05-22/): first two flight runs (Run 1 on 2026-04-21 as negative control, Run 2 on 2026-05-22 as the most recent attempt), with the verified pointing analysis.
- [`debriefings/2026-06-09/`](debriefings/2026-06-09/): Run 3, the first attempt with both a confirmed broadcast and near-target pointing, +X antenna 3-11 deg off Legnica during the captures. Receiver noise floor lifted 4-9 dB with a strongly impulsive character; hypotheses narrowed to the broadcast at low SNR and pulsed ground RFI.
- [`debriefings/2026-06-10/`](debriefings/2026-06-10/): Run 4, a high-elevation pass the upgraded ground station (100 W, ~10 deg beam) likely could not track due to its mount elevation limit, though a beam-miss cannot be confirmed. The lifted impulsive floor reproduced on this second June pass; its FM-incompatible signature, the absent Doppler track, and the April/May-vs-June timing make pulsed ground RFI the strongly preferred cause. A new band-limited hump appears 50-80 kHz below 1296 MHz.
- [`debriefings/2026-07-03/`](debriefings/2026-07-03/): Run 5, a simplified RF-link test proposed by ESOC / TU Graz, with a radio-amateur team based in Oslo transmitting a carrier and the spacecraft recording wideband raw IQ. A signal near 1296 MHz is detected in 5 of 6 snapshots across both passes, +23 to +26 dB over the noise floor, switching with the antenna-rotation pauses and drifting at the pass Doppler rate. The on/off timing and Doppler-rate drift rule out a static internal spur and strongly support an externally received signal. It is FM voice (confirmed by the operators, transmitted wide), recoverable on the ground but hard to make out. The pipeline's FM discriminator is the right demod but it is fed the full wide band, so the voice comes out as static and the on-board speech-to-text transcribes only noise fragments; narrowing to the signal bandwidth before the discriminator recovers it. The detection is in the spectrum, not the demod/STT chain.

