# Adaptive Noise Cancellation

## Status
✅ **Implemented** | ❌ **Not effective for OPS-SAT radio samples**

## Overview

Frequency-domain adaptive filtering using NLMS (Normalized Least Mean Squares) algorithm. Extracts noise reference from initial silence and applies per-bin adaptive filtering to track and cancel noise.

## Algorithm

### Implementation
- **Language**: C
- **Method**: Frequency-domain NLMS adaptive filtering
- **Dependencies**: FFTW3, libsndfile
- **Filter**: 64 taps per frequency bin
- **Step size (MU)**: 0.1 (controls adaptation speed)

### Processing Steps
1. Extract noise reference from initial silence frames
2. Transform signal and noise to frequency domain (FFT)
3. Apply NLMS adaptive filter per frequency bin
4. Adapt filter weights to minimize error
5. Reconstruct time-domain signal (IFFT + overlap-add)

## Usage

```bash
# Inside container
make test-adaptive

# Batch validation
./batch_validate_adaptive.sh
```

Output: `output/opssat_sample_denoised_adaptive.wav`

## Parameters

`src/adaptive_filtering.c`:
- `FILTER_LENGTH`: 64 (taps per frequency bin)
- `MU`: 0.1 (step size, 0.0-1.0)
- `NOISE_EST_FRAMES`: 20 (initial frames for reference)

## Limitations

**Fundamental issue**: Uses fixed noise reference from initial frames, not a continuously updated correlated noise source.

- ❌ Fixed reference can't track non-stationary noise (radio interference)
- ❌ Ineffective when noise characteristics change over time
- ❌ Not true adaptive noise cancellation (no separate reference channel)
- ✅ Works for stationary acoustic noise
- ❌ Fails for OPS-SAT radio interference

**Conclusion**: Like spectral subtraction, this approach assumes stationary noise and fails for dynamic radio interference patterns.

## References

- Haykin, S. (2002). "Adaptive Filter Theory."
- Widrow, B., & Stearns, S. D. (1985). "Adaptive Signal Processing."
