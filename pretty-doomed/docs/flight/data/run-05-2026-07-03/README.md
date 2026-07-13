# Run 5: 2026-07-03 (RF-link test)

- **Spacecraft:** OPS-SAT PRETTY, NORAD 58023
- **Test type:** RF-link test proposed by ESOC / TU Graz: the operators transmit, the spacecraft records wideband raw IQ, and the link is judged directly in the spectrum. The capture was done by ESA's own pipeline on board, not by the experiment.
- **Transmitter:** radio-amateur team based in Oslo (uplink near 1296.0 MHz)
- **SDR:** center 1295.5 MHz, 2.5 MSPS, 2.3 MHz analog bandwidth, manual gain 65 dB. The center is deliberately tuned off the uplink so a 1296.0 MHz signal lands at +500 kHz in the baseband, clear of the AD9361 DC spike.
- **Recordings:** six 2-second raw IQ snapshots (`.cs16`, interleaved little-endian int16 I/Q, ~19 MiB each), three per pass. Pass 1 (19:2x UTC) was transmitted without an amplifier, pass 2 (20:5x UTC) with one.

| Recording | Time (UTC, from filename) | Pass |
|---|---|---|
| `sdr_20260703_192433_1295500000_2500000_1.cs16` | 19:24:33 | 1 (no amplifier) |
| `sdr_20260703_192511_1295500000_2500000_1.cs16` | 19:25:11 | 1 (no amplifier) |
| `sdr_20260703_192531_1295500000_2500000_1.cs16` | 19:25:31 | 1 (no amplifier) |
| `sdr_20260703_205727_1295500000_2500000_1.cs16` | 20:57:27 | 2 (with amplifier) |
| `sdr_20260703_205805_1295500000_2500000_1.cs16` | 20:58:05 | 2 (with amplifier) |
| `sdr_20260703_205825_1295500000_2500000_1.cs16` | 20:58:25 | 2 (with amplifier) |

The raw `.cs16` recordings are too large for the repo and are not included, consistent with how raw IQ is handled for the other runs. What is kept here:

- `sdr_*.xml` — the ION metadata delivered with each recording (timestamp, RF bandwidth, gain mode, per-channel hardware gain and RSSI; the timestamp field is a few seconds after the filename time). The file for `205825` was empty as delivered.
- `run-05-raw-audio.zip` — the minimally processed listenable rendering of each snapshot (tune to the strongest signal, take a ±8 kHz slice with both sidebands, no voice band-pass, no noise reduction), one WAV per recording.

## Result

A signal consistent with the ground transmission is present in five of the six snapshots (all but the first, taken before the signal was up), +23 to +26 dB over the noise floor, keying on and off consistent with the operators' reported antenna-rotation pauses and drifting at the pass Doppler rate. The operators confirmed the transmission was FM voice, sent wide.

All analysis, plots, demodulated and enhanced audio, and conclusions are in the [debriefing](../../debriefings/run-05-2026-07-03/): per-snapshot carrier detection outputs (`carrier-analysis/`), the cross-snapshot comparison (`carriers_summary.*`), and `run-05-audio-processing.zip` with the processed renderings.
