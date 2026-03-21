# SDR Capture Results

## Status

Validated on EM. RX flowgraph integrated into the [`pretty-doomed`](../../../pretty-doomed/) pipeline.

## EM Testing Summary

The sdr-capture experiment was tested iteratively on the OPS-SAT PRETTY EM from v1 through v8.

### Key milestones

- **v1-v2**: Initial deployment. Out-of-bounds `sdr_rate` parameter fixed.
- **v3-v6**: Configuration readback issue discovered. AD9361 parameters were not being applied via GNU Radio's `iio_param_vec_t` mechanism. Fixed in v7 by writing attributes directly via libiio before flowgraph build.
- **v7**: Successful EM run. All three captures showed correct AD9361 readback (frequency 1296 MHz, sample rate 2.4 MSPS, RF bandwidth 200 kHz, gain 50 dB). sc16 files correct size (16 MB). Audio normalized correctly. One capture timed out at 99.4% completion (likely IIO network overhead). Timeout multiplier increased from 2x to 5x.
- **v8**: Current version. Added I/Q constellation BMP generation, per-channel zero fraction diagnostics, millisecond log timestamps. Package version aligned with sdr-loopback.

### I/Q Health

Post-hoc analysis of the v7 sc16 files using the ground tools showed healthy I/Q with balanced zero fractions (~0.37% on both channels), no Q dropout, and clean constellation scatter. The healthy I/Q in RX-only mode, compared to the ~63% Q zeros observed in the loopback experiment (simultaneous TX+RX), is consistent with the hypothesis that the Q dropout is related to concurrent TX/RX streaming rather than an RX path issue.

### Performance on EM

- Wall time: ~49s for 20s capture (ARM CPU processes RX at ~80k SPS vs 200k expected)
- sc16 file size: 16 MB (4M samples at 4 bytes/sample)
- IIO connection: via `ip:10.0.0.1` (network IIO)

## EM Artifacts

Artifacts are not included in the repository.

| Pack ID | Version | Date | Captures | Notes |
|---------|---------|------|----------|-------|
| pack-4023_1772454715 | v2 | 2026-02-25 | 1 | First successful run |
| pack-4023_1772624409 | v3 | 2026-02-27 | 3 | Config readback logging added |
| pack-4023_1772720008 | v5 | 2026-03-01 | 3 | Continued investigation |
| pack-4023_1772797685 | v7 | 2026-03-04 | 3 | IIO config fix, all captures healthy |
