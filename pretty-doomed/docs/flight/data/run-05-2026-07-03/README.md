# Run 5: 2026-07-03 (RF-link test)

- **Recording:** `sdr_20260703_192511_1295500000_2500000_1.cs16`, the zenith snapshot of the first pass, detailed below. Six snapshots were downlinked in all: three from a first pass near 19:25 UTC without an amplifier, three from a second pass near 20:58 UTC with an amplifier. The raw IQ is too large for the repo and is not included, like the other raw IQ downlinks; the cross-snapshot comparison is in `carriers_summary.txt` / `.json` here.
- **Spacecraft:** OPS-SAT PRETTY, NORAD 58023
- **SDR:** center 1295.5 MHz, 2.5 MSPS, 2.3 MHz analog bandwidth, manual gain 65 dB
- **Transmitter:** radio-amateur team based in Oslo (uplink near 1296.0 MHz)
- **Timestamp:** 2026-07-03T19:25:20Z (zenith of the first pass, per TU Graz)
- **Duration:** 2.0 s

## Pre-conditions

This is the simplified RF-link test proposed by ESOC / TU Graz: instead of an FM voice broadcast run through the on-board speech-to-text chain, the operators transmit a carrier so the link can be judged directly in the spectrum, independent of demodulation and detection. The transmitting side this time was a radio-amateur team based in Oslo. Two changes make it interpretable:

- The SDR center was deliberately tuned to **1295.5 MHz**, so a 1296.0 MHz uplink lands at **+500 kHz** in the baseband, clear of the AD9361 DC spike.
- A wide band (2.5 MSPS, 2.3 MHz) raw IQ recording was downlinked, rather than the operational 200 kHz effective baseband.

This recording is the **first pass, at zenith, transmitting without an amplifier**, which the operators noted is the worse case. They kept broadcasting through the pass with short pauses to rotate the antenna.

## Result

**First detected carrier consistent with the reported ground transmission.** A narrowband carrier at **1295.992 MHz** (-8 kHz from 1296.000, consistent with the transmitter's frequency setting plus the small Doppler expected near zenith):

| Metric | Value |
|---|---|
| Carrier offset (baseband) | +491.9 kHz |
| Absolute frequency | 1295.9919 MHz |
| Offset from 1296.000 MHz | -8.1 kHz |
| SNR over noise floor | 22.7 dB (2 s average; higher during the transmit intervals) |
| -3 dB width | 0.61 kHz (the narrow residual carrier; see the modulation note below) |
| -20 dB width | 33.27 kHz |
| On fraction | 67% |
| Keying (100 ms bins) | `#####.....#########.` |
| Doppler drift (carrier) | -353 Hz/s, -636 Hz across carrier-present windows |

The carrier goes on and off over the 2 s (roughly on 0.5 s, off 0.5 s, on 0.9 s), and the off gaps are consistent with the operators' report of pausing to rotate the antenna. Tracked through the clip, its frequency drifts smoothly and monotonically by -353 Hz/s (-636 Hz across carrier-present windows) relative to the detected carrier offset, with the pre-gap and post-gap frequencies on the same smooth trend (the carrier is below threshold during the gap itself); that is a Doppler sweep, consistent in sign and order of magnitude with the ~0.5 kHz/s Doppler rate expected for a near-zenith OPS-SAT pass at 1296 MHz. The on/off timing and Doppler-rate drift rule out a static internal spur and strongly support an externally received signal. This exercises the RF front end and SDR capture, not the FM-demod and speech-to-text chain, which this test bypassed.

There are also broadband impulsive bursts spanning the full 2.5 MHz at ~0.25 s, ~0.8 s, and ~1.75 s (visible in the spectrogram); the 0.8 s burst falls in a carrier-off gap, so they are not simply the carrier switching. Their origin is not established from this clip (candidates: transmit-side splatter or the impulsive platform noise seen in Runs 3 and 4). They do not obscure the carrier.

The signal is present in five of the six snapshots (all but the first, which was taken before the signal was up), each drifting negative at the Doppler rate; see `carriers_summary.txt`. The amplified second pass is at least as strong as the first.

## The signal is FM voice, and what the on-board pipeline would do

The transmission is FM voice, confirmed by the operators (their rig was in FM mode, transmitting wide through the wide FIL1 filter). Once narrowed to the signal bandwidth, the voice is clearly recoverable (syllabic bursts with harmonic formants), though the words are hard to make out and cannot be identified with confidence.

Run through the operational doom pipeline, the FM discriminator is the correct demodulator but it is fed the full wide band, about 170 kHz of noise for a signal a few kHz wide, which pushes it below FM threshold and turns the voice to static: the on-board speech-to-text returns only noise fragments across all six snapshots, not intelligible words. So the link and the voice are real; the gap is bandwidth, and narrowing to about ±10 kHz before the discriminator is estimated to recover on the order of 9 to 11 dB and brings the voice back. The full write-up and the wide-versus-narrowed comparison are in the [debriefing](../../debriefings/2026-07-03/) and [`docs/ONBOARD_PREVIEW.md`](../../ONBOARD_PREVIEW.md).

## Files

Analysis artifacts, generated on the ground from the raw IQ with the tools in `tools/src`:

- `carrier.txt`, `carrier.json` — carrier detection summary for the zenith snapshot ([`detect_carrier.py`](../../../../../tools/src/detect_carrier.py))
- `carriers_summary.txt`, `carriers_summary.json`, `carriers_summary.svg` — comparison across all six snapshots ([`summarize_carriers.py`](../../../../../tools/src/summarize_carriers.py))
- `spectrogram.svg` — full-band waterfall
- `carrier.svg` — spectrogram with the carrier marked, plus its on/off envelope
- `carrier_drift.svg` — carrier frequency vs time, showing the Doppler drift
- `psd.svg` — power spectral density
- `audio-cw.wav` — the carrier demodulated to an audible beat tone ([`demod_audio.py`](../../../../../tools/src/demod_audio.py) `--mode cw`); you can hear it go on and off. FM voice mode is also available for the voice-broadcast runs.
- `rf_test.zip` — all six snapshots demodulated several ways, organized in folders (raw, carrier tone, single-sideband voice, single-sideband voice denoised, on-board FM pipeline, third-party denoised, and the radio-amateur team's own processing), each folder with its own README. The raw IQ is not included.

Reproduce with:

```bash
cd tools/src
python3 detect_carrier.py <recording>.cs16 --sample-rate 2500000 \
    --center-freq 1295.5e6 --target-freq 1296.0e6 --output-dir <dir>
python3 demod_audio.py <recording>.cs16 --sample-rate 2500000 \
    --mode cw --auto --center-freq 1295.5e6 --output-dir <dir>
```

The raw recordings (`sdr_<timestamp>_1295500000_2500000_1.cs16`, ~19 MiB each) and their ION metadata `.xml` are not included in the repo, consistent with how raw IQ is handled for the other runs.
