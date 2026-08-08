# Changelog: v7 to v8

**PRs**: [#141](https://github.com/georgeslabreche/opssat-pretty-doomed/pull/141) (detection-conditional sc16 retention). Ticket: [#140](https://github.com/georgeslabreche/opssat-pretty-doomed/issues/140).

v8 adds a retention mode for the raw I/Q recordings so a run can downlink the captures that carried the voice command without the downlink cost of keeping every capture.

## Detection-Conditional SC16 Retention

**Observation**: Retention of `capture.sc16` was all or nothing. `sdr_keep_sc16=true` keeps all captures of a run (~15 MB each, ~90 MB for six), and `sdr_keep_sc16=false` (the flight default since v5) deletes every one after artifact generation. A planned run wants the raw I/Q of the captures where the voice command was detected, and nothing else.

**Change**: `sdr_keep_sc16` becomes a tri-state ([#141](https://github.com/georgeslabreche/opssat-pretty-doomed/pull/141)). `false` and `true` keep their meaning, and the new `detected` value keeps the sc16 only for captures where the command was detected. The decision keys on the genuine detection, not the trigger, so force-triggered captures (`doom_force_trigger=true`, used in EM testing) without a detection are still deleted in `detected` mode. Kept captures log `SC16 kept (command detected):` beside the existing `SC16 deleted:` line for ground audit. The flight `config.cfg` ships with `sdr_keep_sc16=detected`; a missing key still defaults to `false`, the delete-everything behavior as flown.

The decision logic lives in the pure `should_keep_sc16()` in `config.h`, on the unit-testable side of the build. The pipeline's cleanup path still waits for all artifact consumers (spectrogram, constellation, PSD, postcard) before any delete.

**Verification**: Unit tests cover the tri-state parse (`true`, `1`, `detected`, `false`, `0`, unknown value) and the six-row decision truth table (`make test`: 60 cases, 310 assertions). End to end on the local build with `sdr_keep_sc16=detected` and an attached sc16: a detecting input (`georges_01.wav`) retained the file and logged the keep, and a silent input deleted the file and logged the deletion.
