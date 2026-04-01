# Changelog: v4 to v5

Changes derived from the v4 EM results and operational readiness review.

**PR**: [#85](https://github.com/georgeslabreche/opssat-pretty-doomed/pull/85) (configurable per-capture vs once-per-run SDR init)

## Once-Per-Run SDR Init (Default)

**Observation**: In v4, the AD9361 was re-configured from scratch at the beginning of every capture iteration as a defensive measure against IIO driver state issues. With hardware FIR enabled, this added ~4.5s overhead per capture on ARM32 (tap generation, coefficient loading, clock chain reconfiguration, analog filter recalibration via `ad9361_set_bb_rate_custom_filter_manual()`). Across 4 EM runs (v1 through v4) with multiple captures each, no AD9361 state issue was ever observed.

**Change**: The AD9361 configuration (`configure_ad9361()`) now runs once before the capture loop by default. A new config flag `sdr_init_per_capture` controls the behavior:

- `false` (default): init once before the capture loop. Saves ~4.5s per capture with hardware FIR.
- `true` (robust mode): re-init at the beginning of each capture. Use if captures fail due to AD9361 state issues.

AD9361 lifecycle functions were extracted into `sdr.cpp`/`sdr.h`, separating SDR hardware management from capture logic. `ad9361_configure()` handles the full AD9361 setup (hardware FIR or software-only path, with readback verification). `ad9361_cleanup_fir()` disables the hardware FIR after captures. Both are called from `main.cpp`; `capture.cpp` no longer includes `iio.h` or `ad9361.h` directly.

## FIR Cleanup Moved Earlier

**Observation**: In v4, the hardware FIR was disabled at the very end of the run (after all processing, postcards, etc.). Processing uses WAV files, not the AD9361, so the hardware was held longer than necessary.

**Change**: FIR cleanup now runs right after the capture loop completes, before STT/DOOM/postcard processing. This frees the AD9361 hardware state sooner for subsequent experiments.

## Simplified Run Script

**Observation**: The v4 `run` script executed 3 diagnostic runs with per-run config overrides to compare baseline vs hardware FIR. Now that hardware FIR is validated, the run script no longer needs multiple diagnostic configurations.

**Change**: The `run` script now executes a single run using `config.cfg` directly with no per-run overrides. All operational parameters (6x20s captures, background mode, hardware FIR, concurrent STT loading) are set explicitly in `config.cfg`.

## Explicit Config Values

**Change**: All SDR parameters in `config.cfg` are now uncommented and set explicitly rather than relying on code defaults. Every value has a comment on the line above describing its purpose and unit. Note: the config parser does not support inline comments (only lines starting with `#` are treated as comments).
