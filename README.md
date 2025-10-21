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

**Findings**: Both deep learning and classical methods struggle with non-stationary radio interference patterns unique to OPS-SAT samples.

🎯 **Next**: Explore radio-specific interference cancellation or hybrid approaches

## Experiments

### Neural Network Denoising
[`sandbox/audio-denoiser-dtln/`](./sandbox/audio-denoiser-dtln/) - DTLN with TensorFlow Lite C API
- ✅ Implementation complete, matches official reference
- ✅ Excellent for acoustic noise
- ❌ Limited effectiveness on radio interference
- 📈 [Full results & analysis](./sandbox/audio-denoiser-dtln/README.md)

### Classical Signal Processing
[`sandbox/audio-denoiser-classical/`](./sandbox/audio-denoiser-classical/) - Spectral subtraction, Wiener filtering, adaptive methods
- ✅ Spectral subtraction implemented
- ❌ Ineffective for radio interference (non-stationary noise)
- 🔜 Wiener filtering and adaptive methods planned
- 📈 [Full results & comparison](./sandbox/audio-denoiser-classical/README.md)

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

Classical spectral subtraction:
```bash
cd sandbox/audio-denoiser-classical
docker-compose build && docker-compose up -d
docker-compose exec spectral-denoiser sh
make test
```

## Mission

Making space more interactive, one voice command at a time. 🚀