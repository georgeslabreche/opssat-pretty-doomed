# OPS-SAT PRETTY voice-uplink: flight runs

A small data drop per flight attempt of the PRETTY DOOMed experiment, kept here so the receiver-side record is reproducible without external storage.

## Runs so far

| Run | Date (UTC) | Pack | Attitude | Broadcast | Result |
|---|---|---|---|---|---|
| [01](data/run-01-2026-04-21/) | 2026-04-21 20:14 | `pack-4023_1776802706` | confirmed bad pointing | confirmed transmitted | no detection |
| [02](data/run-02-2026-05-22/) | 2026-05-22 21:51 | `pack-4023_1779486954` | good pointing, +X antenna ~5 deg on target | did not happen | no detection |
| [03](data/run-03-2026-06-09/) | 2026-06-09 21:42 | `pack-4023_1781041655` | near-target pointing, +X antenna 3-11 deg off in captures | confirmed transmitted | no detection, noise floor lifted 4-9 dB with high PAPR |
| [04](data/run-04-2026-06-10/) | 2026-06-10 21:21 | `pack-4023_1781126762` | good pointing, +X antenna 2-14 deg off on a high-elevation pass | attempted, ground antenna likely could not track the pass | no detection, lifted impulsive floor reproduced on a second June pass |
| [05](data/run-05-2026-07-03/) | 2026-07-03 19:25 | RF-link test, wideband IQ | not applicable, RF-link test | FM voice from an Oslo radio-amateur team | signal detected in 5 of 6 snapshots, +23 to +26 dB over floor; externally received per Doppler drift; wide-band FM demod yields static, narrowing before the demod recovers the voice |
| [06](data/run-06-2026-07-27/) | 2026-07-27 10:50 | `pack-4023_1785149761` | tracked Oslo, +X antenna 9-24 deg off, post-zenith deviation | FM voice from the Oslo radio-amateur team | first on-orbit detection: 3 of 6 captures detected and launched DOOM (v7) |
| [07](data/run-07-2026-07-27/) | 2026-07-27 21:36 | `pack-4023_1785188432` | ADCS reset; not commanded, attitude unknown | live human voice (mic, no keyer) from the Oslo team | first detection of a live human voice command: capture 2 detected and launched DOOM; SDR stall lost captures 5-6 |

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

The raw I/Q is too large for the repo and is not included. The WAV audio and the constellation BMPs are also left out, the audio as bulk across many captures and the constellation being a per-capture I/Q health check rather than part of the receiver story; the files above cover that story fully.

Exception: Runs 6 and 7, the first on-orbit detections, also include the complete downlinked archive as a tarball in the data folder (WAV audio, constellations, and the DOOM demo outputs and postcards from the launches). The on-board `sdr_keep_sc16=false` deleted the raw sc16 in flight, so these archives carry no oversized I/Q.

- Run 6: [`data/run-06-2026-07-27/pack-4023_1785149761.tar.gz`](data/run-06-2026-07-27/pack-4023_1785149761.tar.gz)
- Run 7: [`data/run-07-2026-07-27/pack-4023_1785188432.tar.gz`](data/run-07-2026-07-27/pack-4023_1785188432.tar.gz)

Run 5 is an RF-link test rather than a Doom-pipeline run, so `data/run-05-2026-07-03/` instead holds the raw flight record: the recording metadata (`sdr_*.xml`) and `run-05-raw-audio.zip` with the minimally processed rendering of each snapshot. Its raw wideband IQ is not included. The ground analysis (per-snapshot carrier detection in `carrier-analysis/`, the cross-snapshot `carriers_summary` files, and the processed audio in `run-05-audio-processing.zip`) lives with the debriefing. See both READMEs.

## Debriefings

Post-run debriefings are written in markdown under `debriefings/run-NN-YYYY-MM-DD/`, matching the data folder naming. Each debriefing's README is the debriefing itself, with figures alongside it; Runs 6 and 7 are a paired campaign, with Run 6 as the primary write-up and Run 7 as a companion that refers back to it for the shared signal analysis.

- [`debriefings/run-02-2026-05-22/`](debriefings/run-02-2026-05-22/): first two flight runs (Run 1 on 2026-04-21 as negative control, Run 2 on 2026-05-22 as the most recent attempt), with the verified pointing analysis.
- [`debriefings/run-03-2026-06-09/`](debriefings/run-03-2026-06-09/): Run 3, the first attempt with both a confirmed broadcast and near-target pointing, +X antenna 3-11 deg off Legnica during the captures. Receiver noise floor lifted 4-9 dB with a strongly impulsive character; hypotheses narrowed to the broadcast at low SNR and pulsed ground RFI.
- [`debriefings/run-04-2026-06-10/`](debriefings/run-04-2026-06-10/): Run 4, a high-elevation pass the upgraded ground station (100 W, ~10 deg beam) likely could not track due to its mount elevation limit, though a beam-miss cannot be confirmed. The lifted impulsive floor reproduced on this second June pass; its FM-incompatible signature, the absent Doppler track, and the April/May-vs-June timing make pulsed ground RFI the strongly preferred cause. A new band-limited hump appears 50-80 kHz below 1296 MHz.
- [`debriefings/run-05-2026-07-03/`](debriefings/run-05-2026-07-03/): Run 5, a simplified RF-link test proposed by ESOC / TU Graz, with a radio-amateur team based in Oslo transmitting and the spacecraft recording wideband raw IQ. A signal near 1296 MHz is detected in 5 of 6 snapshots across both passes, +23 to +26 dB over the noise floor, switching consistent with the antenna-rotation pauses and drifting at the pass Doppler rate. The on/off timing and Doppler-rate drift rule out a static internal spur and strongly support an externally received signal. It is FM voice (confirmed by the operators, transmitted wide), recoverable on the ground but hard to make out. The pipeline's FM discriminator is the right demod but it is fed the full wide band, so the voice comes out as static and the flight speech-to-text, run on the ground, transcribes only noise fragments; narrowing to the signal bandwidth before the discriminator recovers it. The detection is in the spectrum, not the demod/STT chain.
- [`debriefings/run-06-2026-07-27/`](debriefings/run-06-2026-07-27/): Run 6, the first on-orbit detection of the voice command and the first DOOM launch triggered by it. On the morning pass, with v7 running and the spacecraft tracking Oslo, the radio-amateur team transmitted the looping keyer message; the narrowing stage recovered the voice from the noise floor and three of six captures detected the command and launched DOOM. The three detected captures were the strongest received; pointing error alone did not separate detected from missed captures. The Run 5 fix worked in flight.
- [`debriefings/run-07-2026-07-27/`](debriefings/run-07-2026-07-27/): Run 7, the evening companion to Run 6, and the first on-orbit detection of a live human voice command: the Oslo team spoke the command over a microphone with no TTS keyer. The detection reproduced on this second independent pass (capture 2, exact command match). The ADCS was reset, so the spacecraft was not commanded to track Oslo and its attitude during the pass is unknown; the detection is therefore not evidence about the pointing dependence. An SDR sample-delivery stall lost captures 5 and 6; the pipeline degraded gracefully with no crash.

