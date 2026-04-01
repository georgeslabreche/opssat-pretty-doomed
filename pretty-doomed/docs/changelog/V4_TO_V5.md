# Changelog: v4 to v5

Changes derived from the v4 EM results and operational readiness review.

**PRs**: [#85](https://github.com/georgeslabreche/opssat-pretty-doomed/pull/85) (configurable per-capture vs once-per-run SDR init), [#86](https://github.com/georgeslabreche/opssat-pretty-doomed/pull/86) (ssize_t fix for IIO write return values), [#88](https://github.com/georgeslabreche/opssat-pretty-doomed/pull/88) (two-stage background processing pipeline)

## Once-Per-Run SDR Init (Default)

**Observation**: In v4, the AD9361 was re-configured from scratch at the beginning of every capture iteration as a defensive measure against IIO driver state issues. With hardware FIR enabled, this added ~4.5s overhead per capture on ARM32 (tap generation, coefficient loading, clock chain reconfiguration, analog filter recalibration via `ad9361_set_bb_rate_custom_filter_manual()`). Across 4 EM runs (v1 through v4) with multiple captures each, no AD9361 state issue was ever observed.

**Change**: The AD9361 configuration (`ad9361_configure()`) now runs once before the capture loop by default. A new config flag `sdr_init_per_capture` controls the behavior:

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

## Two-Stage Background Processing Pipeline

**Observation**: In v4 background mode, processing of capture N+1 could not start until the entire pipeline for capture N (DSP + STT + Detection + DOOM + Postcard) completed. On the EM, STT inference takes ~40s while DOOM + Postcard takes ~12-41s. Capture N+1's STT was blocked by capture N's DOOM and Postcard even though they have no dependency on the Transcriber.

**Change**: Split `process_wav()` into two stages:

- **Stage 1** (`process_wav_stt`): Read audio, DSP filtering, STT inference, command detection, write scores/summary. Runs serially across captures (Transcriber is not thread-safe).
- **Stage 2** (`process_wav_exec`): DOOM execution, postcard generation, results.txt append. Fires async after each STT stage completes.

STT inference for capture N+1 starts immediately after STT for capture N finishes, while DOOM and Postcard for capture N run concurrently on a separate thread. Sequential mode is unaffected (uses the combined `process_wav()` which calls both stages inline).

**Known limitation**: Exec stages can run concurrently if DOOM + Postcard for capture N takes longer than STT for capture N+1. This creates a potential race on `doom_demo_index.txt` (demo cycling state) and `results.txt` (append from two threads). On the EM this is unlikely (~40s STT vs ~13-41s DOOM+Postcard) but not impossible. If demo ordering matters, the exec stages would need to be serialized or demo indices pre-assigned during the STT stage.

## ssize_t for IIO Write Return Values

**Observation**: `iio_channel_attr_write()` (the string variant) returns `ssize_t` but was stored in `int` in `pretty_iio.h` and `sdr.cpp`. On ARM32 this was harmless (`ssize_t` is 32-bit), but it's a narrowing conversion on 64-bit platforms. The `_longlong` and `_double` variants correctly return `int` per the libiio API.

**Change**: Use `ssize_t` for `iio_channel_attr_write()` return values in `pretty_iio.h` (shared by all apps) and `sdr.cpp`. Verified all three projects (`pretty-doomed`, `sdr-capture`, `sdr-loopback`) build with zero warnings.

## v5 Results

### Local Emulator

The following timeline was generated from a local Docker emulator test with a 2.5s artificial sleep injected before STT inference to simulate ARM32 latency (x86 STT is near-instant).

**2x1s captures, background, 2.5s simulated STT latency**

![v5 Local Emulator Timeline](data/local-v5/run-00001-timeline.png)

The two-stage pipeline overlap is visible: DOOM #1 + Postcard #1 (Thread 64) runs concurrently with STT Inference #2 (Thread 63). Without the two-stage split, STT #2 would have waited for DOOM #1 and Postcard #1 to complete before starting.

Source data: [data/local-v5/](data/local-v5/).
