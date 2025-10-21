# Spectral Subtraction

## Overview

Spectral subtraction estimates the noise spectrum from silent periods and subtracts it from the noisy signal in the frequency domain. It's fast and simple, but may introduce musical noise artifacts.

## Algorithm

### Processing Steps

1. **Noise estimation**: Estimate noise spectrum from initial silence or noise-only segments
2. **Spectral subtraction**: Subtract estimated noise spectrum from noisy signal in frequency domain
3. **Gain computation**: Calculate spectral gain based on signal-to-noise ratio
4. **Gain floor**: Apply minimum gain to prevent over-suppression
5. **Reconstruction**: Convert back to time domain using overlap-add

### Implementation Details

- **Language**: C
- **Dependencies**: FFTW3 (FFT library), libsndfile (audio I/O)
- **Frame-based processing**: 512 samples (32ms at 16kHz)
- **Overlap**: 50% (256 sample hop size)
- **Windowing**: Hann window for smooth frame transitions
- **Method**: Gain-based spectral subtraction in power domain

## Usage

### Build and Run

```bash
# Inside the Docker container
make clean
make

# Process a single file
./build/spectral_subtraction <input.wav> <output.wav>

# Test with OPS-SAT sample
make test
```

### Batch Validation

Process all NOIZEUS validation samples:

```bash
# Inside the container
./batch_validate_spectral_subtraction.sh
```

This processes ~480 files across 4 noise types and 4 SNR levels.

## Tunable Parameters

Parameters are defined in `src/spectral_subtraction.c`:

### `OVERSUB_FACTOR` (default: 1.0)
Controls how aggressively noise is removed from the signal.

- **Range**: 0.5 - 2.0
- **Default**: 1.0 (moderate noise reduction)
- **Increase to 1.5-2.0**: More aggressive noise removal
  - ✅ Cleaner background, less residual noise
  - ❌ May introduce distortion or "musical noise" artifacts
- **Decrease to 0.5-0.8**: Gentler noise reduction
  - ✅ Cleaner sound, less distortion
  - ❌ More residual background noise remains

### `MIN_GAIN` (default: 0.1)
Minimum spectral gain floor to prevent complete suppression of frequency bins.

- **Range**: 0.0 - 0.3
- **Default**: 0.1 (10% minimum gain)
- **Increase to 0.15-0.3**: More conservative processing
  - ✅ Less distortion, more natural sound
  - ❌ More residual noise remains
- **Decrease to 0.05**: More aggressive suppression
  - ✅ Better noise reduction
  - ❌ May cause "underwater" or "phasing" artifacts

### `NOISE_EST_FRAMES` (default: 20)
Number of initial frames used to estimate the noise spectrum.

- **Range**: 10 - 50
- **Default**: 20 frames (~640ms at 16kHz)
- **Increase to 30-50**: Better noise estimate if you have longer silence
  - ✅ More accurate noise profile
  - ❌ Requires longer initial silence period
- **Decrease to 10-15**: Use less initial audio for estimation
  - ✅ Works with shorter silence periods
  - ❌ Less accurate noise estimate

### `FRAME_SIZE` / `HOP_SIZE` (default: 512 / 256)
FFT window size and overlap for frequency domain processing.

- **FRAME_SIZE**: 512 samples (32ms at 16kHz)
- **HOP_SIZE**: 256 samples (50% overlap)
- ⚠️ **Advanced**: Only change if you understand STFT trade-offs

**After changing parameters**, rebuild inside the container:
```bash
make clean
make test
```

## Test Samples

- **OPS-SAT**: `samples/opssat1/SDRSharp_20240110_180622Z_73841Hz_AF.wav`
  - Output: `output/opssat_sample_denoised_spectral_subtraction.wav`

- **NOIZEUS**: `samples/validation/noizeus/noisy/babble_5db/sp01.wav`
  - Output: `output/validation/noizeus/denoised/babble_5db/sp01.wav`
  - Clean reference: `samples/validation/noizeus/clean/sp01.wav`

## Known Limitations

- **Musical noise**: Spectral subtraction inherently produces "musical noise" artifacts (random tonal artifacts)
- **Noise estimation**: Requires initial silence/noise-only period for accurate noise profiling
- **Stationary noise only**: Works best with stationary noise (constant spectrum over time)
- **Phase preservation**: Preserves original phase, which may not be optimal for heavily corrupted signals
- **Radio interference**: Limited effectiveness on non-stationary radio interference patterns

## References

- Boll, S. (1979). "[Suppression of acoustic noise in speech using spectral subtraction](https://ieeexplore.ieee.org/document/1163209)." IEEE Transactions on Acoustics, Speech, and Signal Processing.
