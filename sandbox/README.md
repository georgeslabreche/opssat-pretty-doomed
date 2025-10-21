# Sandbox Experiments

Proof-of-concept implementations for OPS-SAT voice telecommand denoising.

## Experiments

### 1. Neural Network Denoising
**[`audio-denoiser-dtln/`](./audio-denoiser-dtln/)**

DTLN (Dual-signal Transformation LSTM Network) implementation using TensorFlow Lite C API.

- **Status**: ✅ Complete
- **Results**: Excellent for acoustic noise, limited for radio interference
- **Tech**: TensorFlow Lite C API, LSTM, streaming processing

### 2. Classical Signal Processing
**[`audio-denoiser-classical/`](./audio-denoiser-classical/)**

Classical denoising methods implemented in C.

- **Status**: ✅ Spectral subtraction complete, ineffective for radio interference
- **Results**: Works for stationary acoustic noise, fails for radio interference
- **Methods**:
  - ✅ Spectral Subtraction - implemented and tested
  - 🔜 Wiener Filtering - planned
  - 🔜 Adaptive Noise Cancellation - planned
- **Tech**: FFTW3, libsndfile, Alpine Linux

## Key Findings

Both neural network (DTLN) and classical (spectral subtraction) approaches:
- ✅ **Work well** for stationary acoustic noise (validated on NOIZEUS corpus)
- ❌ **Fail** for non-stationary radio interference in OPS-SAT samples

**Conclusion**: Radio interference differs fundamentally from acoustic noise, requiring specialized radio-specific denoising techniques.

## Next Steps

1. Explore radio-specific interference cancellation techniques
2. Investigate hybrid classical/ML approaches
3. Consider adaptive filtering for non-stationary noise
4. Research signal processing methods specific to SDR/radio applications
