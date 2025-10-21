# Wiener Filtering

## Status
✅ **Implemented** | ❌ **Not effective for OPS-SAT radio samples**

## Overview

Wiener filtering computes optimal MMSE (Minimum Mean-Square Error) gain to estimate clean signal. Uses frequency-dependent gains based on signal-to-noise ratio for smoother noise suppression than spectral subtraction.

## Algorithm

### Implementation
- **Language**: C
- **Method**: Frequency-domain Wiener filtering
- **Dependencies**: FFTW3, libsndfile
- **Optimal gain**: H = clean_power / noisy_power
- **Gain clamping**: [MIN_GAIN, 1.0] to prevent over-suppression/amplification

### Processing Steps
1. Estimate noise power from initial silence frames
2. Transform signal to frequency domain (FFT)
3. Compute clean power: clean_power = noisy_power - noise_power
4. Calculate optimal Wiener gain: H = clean_power / noisy_power
5. Clamp gain to [MIN_GAIN, 1.0]
6. Apply gain and reconstruct (IFFT + overlap-add)

## Usage

```bash
# Inside container
make test-wiener

# Batch validation
./batch_validate_wiener.sh
```

Output: `output/opssat_sample_denoised_wiener.wav`

## Parameters

`src/wiener_filtering.c`:
- `FRAME_SIZE`: 512 samples (32ms at 16kHz)
- `HOP_SIZE`: 256 samples (50% overlap)
- `NOISE_EST_FRAMES`: 20 (initial frames for noise estimation)
- `MIN_GAIN`: 0.1 (minimum gain floor)
- `NOISE_FLOOR`: 1e-10 (numerical stability constant)

## Limitations

**Fundamental issue**: Assumes stationary noise with fixed characteristics over time.

- ❌ Cannot track non-stationary radio interference
- ❌ Fixed noise estimate from initial frames becomes stale
- ❌ Ineffective for OPS-SAT radio samples (non-stationary noise)
- ✅ Works for stationary acoustic noise

**Conclusion**: Like spectral subtraction and adaptive filtering, Wiener filtering assumes stationary noise and fails for dynamic radio interference patterns.

## References

- Wiener, N. (1949). "Extrapolation, Interpolation, and Smoothing of Stationary Time Series."
- Scalart, P., & Filho, J. V. (1996). "Speech enhancement based on a priori signal to noise estimation."
