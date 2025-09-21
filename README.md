# PRETTY DOOMED

First voice command sent to a spacecraft.

## Overview

This project aims to implement voice telecommand capability for the ESA PRETTY spacecraft, starting with the command *"PRETTY, Play DOOM"* and progressing to more significant telecommands like *"PRETTY, Restart SEPP"* or *"PRETTY, Enter Safe Mode."*

## Pipeline

1. **Radio amateur** sends voice telecommand via SDR
2. **GNU Radio** captures and processes the voice signal
3. **DTLN neural network** denoises the audio file
4. **Speech recognition** detects the specific voice command
5. **Execute command** (e.g., play DOOM demo file)
6. **Downlink** all audio and command artifacts

## Current Status

✅ **DTLN Implementation Complete** - TensorFlow Lite C API with streaming processing
📊 **Results**: Limited effectiveness on radio interference
🎯 **Next**: Fine-tune models with OPS-SAT voice data for radio-specific denoising

## Experiments

- [`sandbox/audio-denoiser-dtln/`](./sandbox/audio-denoiser-dtln/) - DTLN denoising with TensorFlow Lite C API
  - ✅ **Working implementation** matching official reference
  - 📈 **Results & next steps** detailed in [experiment README](./sandbox/audio-denoiser-dtln/README.md)

## Documentation

📋 [**Full Proposal**](./docs/PROPOSAL.md) - Detailed project description, proof of concept, and next steps

## Quick Start

```bash
# Audio denoising experiment
cd sandbox/audio-denoiser-dtln
./build-tflite.sh
docker-compose up -d
docker-compose exec dtln-denoiser make
```

## Mission

Making space more interactive, one voice command at a time. 🚀