# PRETTY DOOMED

First voice command sent to a spacecraft.

## Overview

This project aims to implement voice telecommand capability for the ESA PRETTY spacecraft, starting with the command *"PRETTY, Play DOOM"* and progressing to more significant telecommands like *"PRETTY, Restart SEPP"* or *"PRETTY, Enter Safe Mode."*

## Pipeline

1. **Radio amateur** sends voice telecommand via SDR
2. **GNU Radio** captures and processes the voice signal
3. **Audio denoising** removes radio interference and background noise
4. **Speech recognition** detects the specific voice command
5. **Execute command** (e.g., play DOOM demo file)
6. **Downlink** all audio and command artifacts

## Current Status

**Audio Denoising Experiments**:
- ✅ **DTLN (Neural Network)** - TensorFlow Lite C API implementation complete
  - Good for acoustic noise, limited for radio interference
- ✅ **Spectral Subtraction (Classical)** - C implementation complete
  - Works for stationary acoustic noise, fails for radio interference
- ✅ **Adaptive Filtering (Classical)** - NLMS frequency-domain implementation complete
  - Works for stationary acoustic noise, fails for radio interference
- ✅ **Wiener Filtering (Classical)** - MMSE optimal gain implementation complete
  - Works for stationary acoustic noise, fails for radio interference

- ✅ **Adaptive Line Enhancement (Radio-Specific)** - Leaky NLMS with delayed self-reference
  - Implementation complete, validated on NOIZEUS corpus (480 files)
  - Not effective for OPS-SAT radio interference

**Findings**: Deep learning, classical methods (spectral subtraction, adaptive filtering, Wiener filtering), and radio-specific ALE all struggle with the complex non-stationary radio interference patterns in OPS-SAT samples.

🎯 **Next**: Explore robust ASR (Whisper/Wav2Vec2) or hybrid approaches

## Experiments

### Neural Network Denoising
[`sandbox/audio-denoiser-dtln/`](./sandbox/audio-denoiser-dtln/) - DTLN with TensorFlow Lite C API
- ✅ Implementation complete, matches official reference
- ✅ Excellent for acoustic noise
- ❌ Limited effectiveness on radio interference
- 📈 [Full results & analysis](./sandbox/audio-denoiser-dtln/README.md)

### Classical Signal Processing
[`sandbox/audio-denoiser-classical/`](./sandbox/audio-denoiser-classical/) - Spectral subtraction, adaptive filtering, Wiener filtering
- ✅ Spectral subtraction implemented
- ✅ Adaptive filtering (NLMS) implemented
- ✅ Wiener filtering (MMSE) implemented
- ❌ All three methods ineffective for radio interference (non-stationary noise)
- 📈 [Full results & comparison](./sandbox/audio-denoiser-classical/README.md)

### Radio-Specific Interference Cancellation
[`sandbox/audio-denoiser-ale/`](./sandbox/audio-denoiser-ale/) - Adaptive Line Enhancement (ALE)
- ✅ Implementation complete (Leaky NLMS, DELAY=400, FILTER_LENGTH=64)
- ✅ Validated on NOIZEUS corpus (480 files across 4 noise types × 4 SNR levels)
- ❌ Not effective for OPS-SAT radio interference
- Uses delayed self-reference to suppress quasi-periodic interference
- Designed for radio carriers/harmonics, but OPS-SAT interference is too complex
- 📈 [Full documentation](./sandbox/audio-denoiser-ale/README.md)

## Documentation

📋 [**Full Proposal**](./docs/PROPOSAL.md) - Detailed project description, proof of concept, and next steps

## Quick Start

DTLN neural network denoising:
```bash
cd sandbox/audio-denoiser-dtln
./build-tflite.sh
docker-compose up -d
docker-compose exec dtln-denoiser make
```

Classical methods (spectral subtraction, adaptive filtering, Wiener filtering):
```bash
cd sandbox/audio-denoiser-classical
docker-compose build && docker-compose up -d
docker-compose exec classical-denoiser sh
make test
```

ALE radio interference cancellation:
```bash
cd sandbox/audio-denoiser-ale
docker-compose build && docker-compose up -d
docker-compose exec ale-denoiser make test
```

## Mission

Making space more interactive, one voice command at a time. 🚀