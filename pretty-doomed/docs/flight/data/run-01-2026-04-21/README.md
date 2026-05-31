# Run 1: 2026-04-21

- **Pack:** `pack-4023_1776802706`
- **Captures:** 6 x 20 s
- **Spacecraft:** OPS-SAT PRETTY, NORAD 58023
- **Target:** Legnica, Poland

## Pre-conditions

- Attitude: bad pointing, confirmed.
- Broadcast: confirmed transmitted by the operator.

## Result

No detection. The spacecraft pointed away from the ground station during the captures, so the broadcast did not reach the receiver. The captures contain noise-floor-only data and form the negative control for Run 2: a known broadcast that did not show up at the ADC because of attitude.

## Pipeline timing

- AD9361 config: 6.1 s.
- STT model load: 21 s.
- Capture wall time: 20-21 s per capture.
- STT inference: 36-47 s per capture.
- Total run time: 271 s.
- Clean shutdown, no errors.

## Files

The on-board per-capture artifacts under `captures/` follow the naming pattern `capture-NNN-{metrics,psd}.csv`, `capture-NNN-{scores,summary,transcription}.txt`, and `capture-NNN-{spectrogram,psd}.png`. The cross-capture PSD overlay produced on board is at `psd-comparison.png`.
