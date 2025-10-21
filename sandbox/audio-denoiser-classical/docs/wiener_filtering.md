# Wiener Filtering

## Status
🔜 **Not yet implemented**

## Overview

Wiener filtering is an optimal minimum mean-square error (MMSE) filter that estimates the clean signal by minimizing the expected squared error between the clean and estimated signals.

## Planned Approach

- Estimate signal and noise power spectra
- Compute optimal Wiener gain based on signal-to-noise ratio
- Apply frequency-dependent gain to suppress noise
- Less musical noise than spectral subtraction

## Expected Advantages

- Optimal in MMSE sense
- Better performance than spectral subtraction for stationary noise
- Smoother spectral gain transitions reduce musical noise

## Expected Limitations

- Requires accurate noise and signal power estimation
- Still assumes stationary noise
- May introduce some speech distortion

## References

- Wiener, N. (1949). "Extrapolation, Interpolation, and Smoothing of Stationary Time Series."
- Scalart, P., & Filho, J. V. (1996). "Speech enhancement based on a priori signal to noise estimation."
