# Changelog: v5 to v6

**PR**: [#99](https://github.com/georgeslabreche/opssat-pretty-doomed/pull/99) (fix postcard I/Q scatter vertical stripes with weak signals)

## Postcard I/Q Scatter Fix

**Observation**: The postcard blood splatter overlay (`render_iq_scatter` in `postcard_sc16.h`) showed vertical stripes instead of a smooth scatter pattern when processing weak signals. On the v5 EM run, all captures received noise-floor RF (-53 dBFS, `max_abs=78` in int16). Two issues caused the stripes:

1. **Quantization grid**: With only ~157 distinct int16 I values (`max_abs=78`), each value mapped to a specific pixel column, creating a regular grid of vertical dotted lines. Strong signals (thousands of distinct values) do not exhibit this because the grid spacing is sub-pixel.

2. **Range factor too aggressive**: The original `range = max_abs * 0.35f` zoomed into the inner 35% of peak amplitude. For strong signals this creates a wide blood splatter effect (points spread across the frame). For weak signals it clipped ~65% of samples to the plot edges, concentrating them into dense vertical stripes at the left and right borders.

The standalone `constellation.bmp` (using `range = max_abs * 1.1f`) rendered correctly for the same data.

**Fix**: Three changes to `render_iq_scatter()`:

- **Adaptive range**: Use `max_abs * 0.35f` (aggressive spread) for strong signals (`max_abs >= 200`) and `max_abs * 1.1f` (10% margin, no clipping) for weak signals (`max_abs < 200`). The threshold of 200 (~-44 dBFS) separates noise-floor captures from signals with enough distinct values for the aggressive spread to work.

- **Dithering**: Add +/-0.5 LSB random jitter to each I/Q sample before pixel mapping. This breaks up the quantization grid for weak signals where few distinct int16 values would otherwise map to a sparse set of pixel columns. For strong signals the dither is negligible relative to the signal amplitude.

- **Alpha boost**: Scale blend alpha up to 2x for weak signals (`max_abs < 250`). Uniform noise distributes points evenly across the plot with no clustering, so per-pixel density is low. Strong signals naturally cluster on constellation points, building up intensity through overlap.

**Before** (v5, EM Run 1 Capture 2): vertical stripes from quantization grid and edge clipping.

![Before](data/local-v6/postcard-before.png)

**After** (v6, same sc16 data): smooth scatter with dithering, adaptive range, and alpha boost.

![After](data/local-v6/postcard-after.png)

## Flight Run Script

**Change**: Updated the `run` script to separate listen-only and DOOM-triggered runs. Run 1 captures without triggering DOOM (detection only). Run 2 force-triggers DOOM on every capture. This validates both paths in a single execution. The v5 layout (verification + operational) was replaced with this split.

## File Input sc16 Support

**Change**: Added `-q <sc16_file>` CLI flag to provide an sc16 file in file input mode (`-i`). Previously, postcard I/Q scatter and spectrogram rendering were only available in SDR capture mode. This enables testing postcard rendering with specific sc16 files without running the SDR pipeline.

## Standalone Postcard Scatter Test

**Change**: Added `tests/test_postcard_scatter.cpp`, a standalone test program for postcard I/Q scatter rendering. Generates a postcard from an sc16 file and a DOOM frame without requiring STT, DSP, or SDR dependencies.

```bash
./build/local/test_postcard_scatter <sc16_file> <frame_jpg> <output_png> [scale]
```
