# EM Verification Results

Verification results from experiment runs on the OPS-SAT Engineering Model (EM), ARM32 dual-core SEPP. See [TESTING.md](../TESTING.md) for the test environment. On-orbit flight results live separately in [`flight/`](../flight/).

For previous EM results, see [V3_TO_V4.md](V3_TO_V4.md) (v4) and [V2_TO_V3.md](V2_TO_V3.md) (v3). For the v5 changes, see [V4_TO_V5.md](V4_TO_V5.md). For the v6 changes (postcard scatter fix, flight run script), see [V5_TO_V6.md](V5_TO_V6.md).

## v6 Validation

Data from SMILE artifact [`pack-4023_1775579267`](data/em-v6/pack-4023_1775579267/). The v6 patch fixed a postcard I/Q scatter rendering bug and simplified the run script for flight. The v6 EM run validated both execution paths:

- **Run 1** (3 x 20s, listen-only): all captures, diagnostics, and SC16 cleanup confirmed. Total: 147.3s.
- **Run 2** (3 x 20s, DOOM force-triggered): all 3 DOOM demos ran, postcards generated with smooth I/Q scatter (no stripes). Total: 164.9s.

Capture wall times (20-22s), STT inference (36-45s), and I/Q metrics are consistent with v5. Full v6 EM analysis in [changelog/V5_TO_V6.md](V5_TO_V6.md#v6-results).

## v6 Full Package Verification

Data from SMILE artifact [`pack-4023_1775661470`](data/em-v6/pack-4023_1775661470/). Full v6 SEPP package installed and run with the flight configuration: single run, 6 x 20s captures, `doom_force_trigger=false`.

All 6 captures completed successfully (20-21s each). No command detected (expected). STT inference: 47.0s, 42.3s, 37.2s, 36.2s, 36.0s, 36.2s. SC16 files deleted after diagnostics. Total: 270.0s (~4.5 min). I/Q metrics consistent with previous runs.

![Run 1](data/em-v6/pack-4023_1775661470/run-00001-timeline-and-resource.png)

## v5 Detailed Results

Data from SMILE artifact [`pack-4023_1775155417`](data/em-v5/pack-4023_1775155417/).

## Run Configuration

| | Run 1 (sc16 preserved) | Run 2 (sc16 deleted) |
|---|---|---|
| Captures | 2 x 20s | 6 x 20s |
| sdr_keep_sc16 | true | false |
| Decimation | Hardware FIR (600 kSPS, 3x) | Hardware FIR (600 kSPS, 3x) |
| Process mode | background | background |
| sdr_init_per_capture | false (init once) | false (init once) |
| stt_concurrent_load | true | true |
| doom_force_trigger | true | true |

All runs: 1296 MHz, 200 kHz effective sample rate, 16 kHz audio output, sdr_timeout_multiplier=2.

## Timing Summary

### Run 1 (2 captures, sc16 preserved)

| Metric | Value |
|---|---|
| SDR Config (once) | 6.2s |
| Capture 1 wall time | 20s |
| Capture 2 wall time | 21s |
| STT 1 inference | 40.1s |
| STT 2 inference | 37.4s |
| LPF taps | 97 |
| **Total time** | **130.7s** |

### Run 2 (6 captures, sc16 deleted)

| Metric | Cap 1 | Cap 2 | Cap 3 | Cap 4 | Cap 5 | Cap 6 |
|---|---|---|---|---|---|---|
| Capture wall time | 20s | 20s | 20s | 23s | 23s | 22s |
| STT inference | 48.5s | 51.2s | 41.1s | 36.7s | 36.3s | 36.4s |

SDR Config (once): 4.4s. LPF taps: 97. **Total time: 295.0s.**

## Key Findings

### Once-per-run SDR init

The AD9361 configuration (FIR tap generation, clock chain, analog filter calibration) runs once before the capture loop. Run 2 shows a single 4.4s SDR Config phase, then 6 captures back-to-back with no re-initialization gaps. This saves ~26s compared to v4's per-capture init for 6 captures.

### Near-continuous capture

Captures 1-3 complete in 20s (matching configured duration). Captures 4-6 take 22-23s, likely due to CPU contention from concurrent STT inference. The flowgraph rebuild between captures takes ~17ms (not visible at this scale).

### Two-stage background processing

The STT chain processes captures sequentially (Transcriber is not thread-safe), while DOOM + Postcard fire async on separate threads. The timeline shows STT for capture N+1 starting immediately after STT for capture N completes, with DOOM/Postcard running concurrently.

STT times range from 36-51s. The first two captures (48.5s, 51.2s) are slower, likely due to concurrent SDR capture activity on the other core. Once all captures finish, STT times stabilize at 36-41s.

### sc16 cleanup

Run 2 confirms sc16 files are deleted after each capture's artifacts and postcard are generated. All 6 sc16 deletions logged. Run 1 confirms sc16 preservation with `sdr_keep_sc16=true`.

### Onboard diagnostics

All new v5 artifacts generated successfully for all captures:
- `capture-metrics.csv`: I/Q diagnostics (RMS, peak, PAPR, DC offset, imbalance, zero fraction)
- `capture-psd.csv` + `capture-psd.bmp`: PSD with 7811 Welch segments per capture
- `psd-comparison.bmp`: cross-capture PSD overlay (Run 2)
- Spectrogram and constellation BMPs with axis labels

### Downlink size

| | Uncompressed | tar.gz |
|---|---|---|
| Run 1 (2 captures, sc16 preserved) | 45 MB | ~26 MB |
| Run 2 (6 captures, sc16 deleted) | 33 MB | 23 MB |
| Full toGround (both runs) | 78 MB | 49 MB |

The 6-capture run without sc16 (23 MB compressed) is smaller than v4's 2-capture run with sc16 (~70 MB compressed).

## Run 1: 2 captures, sc16 preserved

![Run 1](data/em-v5/pack-4023_1775155417/run-00001-timeline-and-resource.png)

Two captures back-to-back. SDR Config (4.4s, teal) visible at start. Two-stage pipeline: STT 1 (40.2s) completes, DOOM 1 (20.4s) + Postcard fires async, STT 2 (37.5s) starts immediately. sc16 files preserved (used in postcard blood splatter overlay).

## Run 2: 6 captures, sc16 deleted

![Run 2](data/em-v5/pack-4023_1775155417/run-00002-timeline-and-resource.png)

Six captures on the main thread, near-continuous. STT chain processes sequentially across background threads. DOOM + Postcard exec stages fire async after each STT completes. sc16 deleted after each capture's postcard is generated.

## Regenerating Plots

```bash
python3 scripts/plots/plot_resource.py --batch \
  --input-dir docs/changelog/data/em-v5/pack-4023_1775155417 \
  --output-dir docs/changelog/data/em-v5/pack-4023_1775155417
```

Requires `matplotlib` and `numpy` (`pip install matplotlib numpy`).

Source data: [changelog/data/em-v5/](data/em-v5/).
