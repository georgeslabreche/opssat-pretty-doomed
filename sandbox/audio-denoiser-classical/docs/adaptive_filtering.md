# Adaptive Noise Cancellation

## Status
🔜 **Not yet implemented**

## Overview

Adaptive filtering uses a reference noise signal and an adaptive filter (e.g., LMS or NLMS algorithm) to continuously estimate and cancel noise from the corrupted signal.

## Planned Approach

- Adaptive filter (LMS/NLMS) adjusts coefficients in real-time
- Requires a reference noise signal (challenge for radio samples)
- Continuously adapts to changing noise characteristics
- Can handle non-stationary noise

## Expected Advantages

- Can track time-varying noise
- No need for explicit noise spectrum estimation
- Works with non-stationary noise sources
- May handle radio interference better than spectral methods

## Expected Limitations

- Requires a reference noise signal (correlated with actual noise)
- May be challenging to obtain clean reference for radio interference
- Computational complexity depends on filter order
- Convergence time may introduce latency

## Potential Variants

- **Normalized LMS (NLMS)**: Better convergence properties
- **Recursive Least Squares (RLS)**: Faster convergence, higher complexity
- **Affine Projection Algorithm (APA)**: Balance between LMS and RLS

## References

- Widrow, B., & Stearns, S. D. (1985). "Adaptive Signal Processing."
- Haykin, S. (2002). "Adaptive Filter Theory."
