# Classical Audio Denoising Methods

Evaluation of classical signal processing methods for denoising OPS-SAT radio amateur voice broadcasts.

## Methods

| Method | Status | Documentation | Batch Script |
|--------|--------|---------------|--------------|
| **Spectral Subtraction** | ✅ Implemented | [docs/spectral_subtraction.md](docs/spectral_subtraction.md) | `batch_validate_spectral_subtraction.sh` |
| **Adaptive Filtering** | ✅ Implemented | [docs/adaptive_filtering.md](docs/adaptive_filtering.md) | `batch_validate_adaptive.sh` |
| **Wiener Filtering** | 🔜 Planned | [docs/wiener_filtering.md](docs/wiener_filtering.md) | `batch_validate_wiener.sh` |

## Results Summary

### Spectral Subtraction

**Status**: ❌ **Not effective for OPS-SAT radio samples**

- **Original**: [`samples/opssat1/SDRSharp_20240110_180622Z_73841Hz_AF.wav`](../../samples/opssat1/SDRSharp_20240110_180622Z_73841Hz_AF.wav)
- **Denoised**: [`output/opssat_sample_denoised_spectral_subtraction.wav`](output/opssat_sample_denoised_spectral_subtraction.wav)


**Findings**:
- Works reasonably well for **stationary acoustic noise** (tested on [NOIZEUS corpus](../../samples/validation/download_noizeus.sh))
- **Fails for radio interference** present in OPS-SAT samples
- Introduces audible distortion and musical noise artifacts
- Cannot handle non-stationary radio interference patterns

**Conclusion**: Spectral subtraction is **insufficient** for OPS-SAT radio denoising. The radio interference is non-stationary and differs fundamentally from acoustic noise, requiring more sophisticated approaches.

### Adaptive Filtering (NLMS)

**Status**: ❌ **Not effective for OPS-SAT radio samples**

- **Original**: [`samples/opssat1/SDRSharp_20240110_180622Z_73841Hz_AF.wav`](../../samples/opssat1/SDRSharp_20240110_180622Z_73841Hz_AF.wav)
- **Denoised**: [`output/opssat_sample_denoised_adaptive.wav`](output/opssat_sample_denoised_adaptive.wav)

**Findings**:
- Uses fixed noise reference from initial frames
- Cannot track non-stationary radio interference
- ✅ Works for stationary acoustic noise
- ❌ Fails for OPS-SAT radio interference

**Conclusion**: Like spectral subtraction, adaptive filtering assumes stationary noise and fails for dynamic radio interference patterns.

## Project Structure

```
audio-denoiser-classical/
├── README.md                   # This file (index and results)
├── docs/                       # Method-specific documentation
│   ├── spectral_subtraction.md
│   ├── adaptive_filtering.md
│   └── wiener_filtering.md
├── src/                        # Source code
│   ├── spectral_subtraction.c
│   ├── adaptive_filtering.c
│   └── common/                 # Shared utilities
│       ├── audio_io.c
│       └── audio_io.h
├── output/                     # Denoised outputs (.gitignored)
├── Dockerfile                  # Alpine Linux build environment
├── docker-compose.yml
├── Makefile
└── batch_validate_*.sh         # Validation scripts
```
