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

**GNU Radio Signal Processing**:
- ✅ **Signal Processor** - Lowpass → Bandpass → Power Squelch pipeline
  - Voice-optimized (300-3400 Hz), ready for SEPP deployment
- ✅ **GNU Radio Libraries** - Built from source for ARM32 (armv7l)
  - Includes gr-iio for AD9361/PlutoSDR support

**GNU Radio + Whisper Integration**:
- ✅ **gnuradio-whisper** - Single binary pipeline
  - GNU Radio signal processing + Whisper speech-to-text
  - Tested with clean, noisy, and very noisy voice samples

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

## Experiments

### GNU Radio Signal Processing
[`sandbox/gnuradio/`](./sandbox/gnuradio/) - Signal processing for OPS-SAT SEPP

- [`signal-processor/`](./sandbox/gnuradio/signal-processor/) - Voice audio pipeline (Lowpass → Bandpass → Squelch)
- [`build-libs-armv7/`](./sandbox/gnuradio/build-libs-armv7/) - GNU Radio libraries built for ARM32

### GNU Radio + Whisper Integration
[`sandbox/integrations/gnuradio-whisper/`](./sandbox/integrations/gnuradio-whisper/) - End-to-end pipeline
- GNU Radio signal processing + Whisper speech-to-text in a single binary
- 📈 [Full documentation](./sandbox/integrations/gnuradio-whisper/README.md)

### Neural Network Denoising
[`sandbox/denoising/audio-denoiser-dtln/`](./sandbox/denoising/audio-denoiser-dtln/) - DTLN with TensorFlow Lite C API
- ✅ Implementation complete, matches official reference
- ✅ Excellent for acoustic noise
- ❌ Limited effectiveness on radio interference
- 📈 [Full results & analysis](./sandbox/denoising/audio-denoiser-dtln/README.md)

### Classical Signal Processing
[`sandbox/denoising/audio-denoiser-classical/`](./sandbox/denoising/audio-denoiser-classical/) - Spectral subtraction, adaptive filtering, Wiener filtering
- ✅ Spectral subtraction implemented
- ✅ Adaptive filtering (NLMS) implemented
- ✅ Wiener filtering (MMSE) implemented
- ❌ All three methods ineffective for radio interference (non-stationary noise)
- 📈 [Full results & comparison](./sandbox/denoising/audio-denoiser-classical/README.md)

### Radio-Specific Interference Cancellation
[`sandbox/denoising/audio-denoiser-ale/`](./sandbox/denoising/audio-denoiser-ale/) - Adaptive Line Enhancement (ALE)
- ✅ Implementation complete (Leaky NLMS, DELAY=400, FILTER_LENGTH=64)
- ✅ Validated on NOIZEUS corpus (480 files across 4 noise types × 4 SNR levels)
- ❌ Not effective for OPS-SAT radio interference
- Uses delayed self-reference to suppress quasi-periodic interference
- Designed for radio carriers/harmonics, but OPS-SAT interference is too complex
- 📈 [Full documentation](./sandbox/denoising/audio-denoiser-ale/README.md)

## Documentation

- 📋 [**Full Proposal**](./docs/PROPOSAL.md) - Detailed project description, proof of concept, and next steps
- 🛰️ [**SEPP Reference**](./SEPP.md) - OPS-SAT Satellite Experimental Processing Platform details

## Quick Start

GNU Radio signal processor:
```bash
cd sandbox/gnuradio/signal-processor
./setup-libs.sh
docker-compose build
docker-compose run --rm signal-processor make test
```

GNU Radio + Whisper integration:
```bash
cd sandbox/integrations/gnuradio-whisper
docker-compose build
docker-compose run gnuradio-whisper make test-georges-full
```

DTLN neural network denoising:
```bash
cd sandbox/denoising/audio-denoiser-dtln
./build-tflite.sh
docker-compose up -d
docker-compose exec dtln-denoiser make
```

Classical methods (spectral subtraction, adaptive filtering, Wiener filtering):
```bash
cd sandbox/denoising/audio-denoiser-classical
docker-compose build && docker-compose up -d
docker-compose exec classical-denoiser sh
make test
```

ALE radio interference cancellation:
```bash
cd sandbox/denoising/audio-denoiser-ale
docker-compose build && docker-compose up -d
docker-compose exec ale-denoiser make test
```

## Mission

Making space more interactive, one voice command at a time. 🚀
