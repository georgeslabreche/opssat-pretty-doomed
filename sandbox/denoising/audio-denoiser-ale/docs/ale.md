# Adaptive Line Enhancement (ALE)

## Status
✅ **Implemented** | ❌ **Not effective for OPS-SAT radio samples**

## Overview

Adaptive Line Enhancement (ALE) is designed specifically for removing quasi-periodic interference (e.g., radio carriers, harmonics) while preserving non-periodic signals like speech. Unlike traditional adaptive filters that use a separate noise reference, ALE uses a delayed version of the input signal itself.

## Algorithm

### Key Concept

ALE exploits the difference in temporal correlation between:
- **Speech**: Low autocorrelation beyond ~20-30ms
- **Periodic interference**: High autocorrelation at fundamental period

By delaying the input signal by Δ samples (where Δ > speech correlation time), the delayed reference is decorrelated from speech but still correlated with periodic interference.

### Implementation

- **Language**: C
- **Method**: Time-domain adaptive filtering with delayed reference
- **Dependencies**: libsndfile
- **Algorithm**: Leaky Normalized LMS (NLMS)

### Processing Steps

1. **Initialize**: Set up adaptive filter weights (zeros), delay line, reference buffer
2. **For each sample**:
   - Get delayed reference: `x(n-Δ)` from circular buffer
   - Update delay line with delayed reference
   - Compute prediction: `y(n) = w^T * x_delayed` (interference estimate)
   - Compute error: `e(n) = x(n) - y(n)` (desired output = speech)
   - Update weights: `w(n+1) = λ*w(n) + μ*e(n)*x_delayed`
   - Store current sample in reference buffer

3. **Output**: Error signal `e(n)` contains speech with suppressed periodic interference

## Parameters

`src/ale_denoiser.c`:
- `DELAY`: 400 samples (25ms at 16kHz) - decorrelates speech, preserves interference correlation
- `FILTER_LENGTH`: 64 taps - adaptive filter order
- `MU`: 0.01 - LMS step size (learning rate)
- `LEAK_FACTOR`: 0.9999 - prevents weight drift

### Parameter Tuning

**DELAY (Δ)**:
- Too small: Speech remains correlated, gets suppressed
- Too large: Interference becomes decorrelated, not removed
- Rule of thumb: 10-50ms for speech (160-800 samples at 16kHz)
- For radio interference with known carrier period: Δ ≈ period

**FILTER_LENGTH (M)**:
- Longer: Better tracking of complex interference patterns
- Shorter: Faster adaptation, lower computation
- Typical range: 32-128 taps

**MU (μ)**:
- Higher: Faster adaptation, more noise
- Lower: Slower adaptation, more stable
- Typical range: 0.001-0.1

## Usage

```bash
# Inside container
make test

# Batch validation on NOIZEUS
./batch_validate_ale.sh
```

Output: `output/opssat_sample_denoised_ale.wav`

## Theoretical Basis

ALE was introduced by Widrow et al. (1975) for enhancing periodic signals in noise. The optimal delay maximizes:

**Correlation Ratio**: ρ(Δ) = autocorrelation of interference / autocorrelation of speech

For speech (rapidly decorrelating) + periodic interference (slowly decorrelating):
- At Δ=0: Both signals correlated (no separation)
- At Δ=25ms: Speech decorrelated, interference still correlated (optimal)
- At Δ>100ms: Both decorrelated (no enhancement)

## Advantages for Radio Interference

1. **No separate noise reference needed**: Self-referencing using delay
2. **Tracks time-varying interference**: Adaptive weights follow slow changes
3. **Preserves speech phase**: Only amplitude/spectral shaping
4. **Low complexity**: Real-time capable for spacecraft

## Limitations

- **Fixed delay**: Cannot adapt to changing interference characteristics
- **Assumes periodic interference**: May not work for wideband/random noise
- **Convergence time**: Requires several hundred samples to adapt
- **Initial transient**: First DELAY samples have artifacts

## Actual Results

**For acoustic noise** (NOIZEUS corpus):
- ✅ Works for stationary acoustic noise
- Validated on 480 files (4 noise types × 4 SNR levels)

**For radio interference** (OPS-SAT samples):
- ❌ **Not effective for OPS-SAT radio interference**
- Interference is too complex and non-stationary
- ALE assumes quasi-periodic patterns with consistent temporal structure
- OPS-SAT interference lacks the regularity required for delayed self-reference approach

## Why ALE Failed for OPS-SAT

1. **Assumption violation**: ALE assumes interference autocorrelation remains high at delay Δ
2. **Non-stationary interference**: OPS-SAT patterns change too rapidly for adaptation
3. **Complex interference**: Not simple carriers/harmonics, but multi-component interference
4. **Fixed delay limitation**: Cannot adapt delay to track changing interference period

## References

- Widrow, B., et al. (1975). "Adaptive Noise Cancelling: Principles and Applications." Proceedings of the IEEE, 63(12), 1692-1716.
- Zeidler, J. R. (1990). "Performance analysis of LMS adaptive prediction filters." Proceedings of the IEEE, 78(12), 1781-1806.
