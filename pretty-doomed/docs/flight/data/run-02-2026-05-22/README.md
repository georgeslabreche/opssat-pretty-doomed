# Run 2: 2026-05-22

- **Pack:** `pack-4023_1779486954`
- **Captures:** 6 x 20 s
- **Spacecraft:** OPS-SAT PRETTY, NORAD 58023
- **Target:** Legnica, Poland
- **Experiment moment:** 2026-05-22T21:52:21Z

## Pre-conditions

- Attitude: good pointing, verified. The +X antenna was 4.9 deg off Legnica at the scheduled second, with a 2.7 deg minimum, and held within ~15 deg across all six captures. Convention: ECI, body-to-inertial, scalar last.
- Broadcast: did not happen. The operator could not get their station running at the scheduled time.

## Result

No detection. The receive chain and pointing were correct, but no broadcast was transmitted, so there was nothing for the spacecraft to capture. The captures contain noise-floor-only data, statistically indistinguishable from Run 1.

## Pipeline timing

- AD9361 config: 5.8 s.
- STT model load: 21 s.
- Capture wall time: 21-22 s per capture.
- STT inference: 36-51 s per capture.
- Total run time: 283 s.
- Clean shutdown, no errors.

## Notes

- RF frontend init: TU Graz noted a missing hardware initialisation step on the frontend, upstream of the experiment script. Our AD9361 init sequence is clean in the log. Benign on our end.
- The spacecraft exits Earth's shadow at ~21:50:36 UTC, about 105 s before the experiment moment, so it is sunlit through all six captures.

## Files

The on-board per-capture artifacts under `captures/` follow the naming pattern `capture-NNN-{metrics,psd}.csv`, `capture-NNN-{scores,summary,transcription}.txt`, and `capture-NNN-{spectrogram,psd}.png`. The cross-capture PSD overlay produced on board is at `psd-comparison.png`.
