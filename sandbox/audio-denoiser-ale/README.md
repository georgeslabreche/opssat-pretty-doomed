# Adaptive Line Enhancement (ALE) for Radio Interference Cancellation

Evaluation of Adaptive Line Enhancement for removing quasi-periodic radio interference from OPS-SAT voice broadcasts.

## Method

| Method | Status | Documentation | Batch Script |
|--------|--------|---------------|--------------|
| **Adaptive Line Enhancement (ALE)** | ✅ Implemented | [docs/ale.md](docs/ale.md) | `batch_validate_ale.sh` |

## Approach

Unlike classical methods (spectral subtraction, Wiener filtering) that assume stationary noise, ALE is designed for **periodic/quasi-periodic interference** common in radio communications:

- Uses **delayed self-reference** instead of separate noise estimate
- Adaptive filter learns to predict periodic components (interference)
- Error signal (input - prediction) contains desired speech
- Works for time-varying periodic interference (e.g., carrier tones, harmonics)

## Results Summary

### Adaptive Line Enhancement

**Status**: ❌ **Not effective for OPS-SAT radio samples**

- **Original**: [`samples/opssat1/SDRSharp_20240110_180622Z_73841Hz_AF.wav`](../../samples/opssat1/SDRSharp_20240110_180622Z_73841Hz_AF.wav)
- **Denoised**: [`output/opssat_sample_denoised_ale.wav`](output/opssat_sample_denoised_ale.wav)

**Findings**:
- ✅ Works for stationary acoustic noise (validated on NOIZEUS corpus)
- ❌ Fails for OPS-SAT radio interference
- ALE assumes interference is quasi-periodic with consistent temporal structure
- OPS-SAT interference is too complex and non-stationary for delayed self-reference approach

**Conclusion**: ALE joins spectral subtraction, adaptive filtering, and Wiener filtering in failing for OPS-SAT radio interference. The interference patterns are too complex for classical time-domain or frequency-domain approaches.

## Quick Start

```bash
# Build container
docker-compose build && docker-compose up -d

# Build and test on OPS-SAT sample
docker-compose exec ale-denoiser make test

# Batch validation on NOIZEUS corpus
docker-compose exec ale-denoiser sh
./batch_validate_ale.sh
```

## Project Structure

```
audio-denoiser-ale/
├── README.md                   # This file
├── docs/
│   └── ale.md                  # Detailed algorithm documentation
├── src/
│   ├── ale_denoiser.c          # ALE implementation
│   └── common/                 # Shared utilities
│       ├── audio_io.c
│       └── audio_io.h
├── output/                     # Denoised outputs (.gitignored)
├── Dockerfile                  # Alpine Linux build environment
├── docker-compose.yml
├── Makefile
└── batch_validate_ale.sh       # Validation script
```

## Why ALE for Radio Interference?

Classical methods (spectral subtraction, adaptive filtering, Wiener filtering) **all failed** for OPS-SAT samples because:
1. They assume **stationary noise** with fixed spectral characteristics
2. Radio interference is **non-stationary** and **quasi-periodic**

ALE is fundamentally different:
- Designed for **periodic interference cancellation**
- No stationarity assumption required
- Tracks **time-varying periodic components**
- Widely used in communications for carrier/tone removal

## Key Parameters

- **DELAY**: 400 samples (25ms at 16kHz) - decorrelates speech, preserves interference correlation
- **FILTER_LENGTH**: 64 taps - controls interference tracking complexity
- **MU**: 0.01 - learning rate for adaptation speed

See [docs/ale.md](docs/ale.md) for parameter tuning guidance.
