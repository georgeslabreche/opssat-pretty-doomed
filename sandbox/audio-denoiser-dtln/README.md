# DTLN Audio Denoiser with TensorFlow Lite C API

## Goal
Validate DTLN audio denoising using TensorFlow Lite C interpreter for the PRETTY voice telecommand system.

## Test Files
- Input: `../../samples/SDRSharp_20240110_180622Z_73841Hz_AF.wav` (noisy radio amateur broadcast, 46875 Hz stereo)
- Denoised reference: `../../samples/SDRSharp_20240110_180622Z_73841Hz_AF_DENOISED.wav` (denoised with online tool)

## Getting Started

Required: Docker and Docker Compose.

### Platform Support
This setup builds TensorFlow Lite C API for **ARM64** by default. For other platforms, use the `--config` flag:

- **ARM64** (default): `./build-tflite.sh`
- **Intel x86_64**: `./build-tflite.sh --config=x86_64`
- **ARM 32-bit**: `./build-tflite.sh --config=elinux_armhf`
- **Android ARM64**: `./build-tflite.sh --config=android_arm64`

For help: `./build-tflite.sh --help`

For more cross-compilation options, see: https://ai.google.dev/edge/litert/build/arm

### First-Time Setup

1. **Build TensorFlow Lite C API** (one-time, ~10-15 minutes):
   ```bash
   cd sandbox/audio-denoiser-dtln
   ./build-tflite.sh
   ```
   This compiles TensorFlow Lite C API and saves it to `libs/` directory.

2. **Build the application**:
   ```bash
   docker-compose build
   docker-compose up -d
   ```

3. **Run the denoiser**:
   ```bash
   docker-compose exec dtln-denoiser sh

   # Build and run both OPS-SAT and DTLN validation tests
   make

   # Or run individual commands:
   # make test      # Process OPS-SAT radio sample -> opssat_sample_denoised.wav
   # make validate  # Process DTLN air conditioning sample -> dtln_ac_sample_denoised_validation.wav
   ```

### Quick Start (if libs/ already exists)
```bash
docker-compose build
docker-compose up -d
docker-compose exec dtln-denoiser sh
make  # Runs both test and validate
```

## Algorithm Flow

The DTLN denoiser implements a streaming dual-stage neural network:

### 1. Audio Preprocessing
- **Load WAV file** using libsndfile
- **Convert stereo to mono** (left channel) and **resample to 16kHz** if needed
- **Conditional processing**: Skip intermediate files when input is already 16kHz mono

### 2. Streaming Processing (Official DTLN Implementation)
- **Sliding buffer approach**: 512-sample input buffer with 128-sample shifts (8ms steps)
- **Real FFT**: Extract magnitude/phase from input buffer
- **Model 1**: LSTM-based magnitude masking in frequency domain
- **Reconstruction**: Apply mask to original complex spectrum, preserve phase
- **Model 2**: LSTM-based time domain enhancement
- **Overlap-add**: Stream output with proper buffer management

### Key Implementation Details
- **Matches official Python reference**: Buffer-based streaming, real FFT/IFFT
- **LSTM state management**: Persistent states across audio blocks for temporal modeling
- **Memory efficient**: Processes 8ms chunks, suitable for real-time deployment

## Results

### ✅ DTLN Air Conditioning Sample
- **Excellent denoising quality** matching official reference
- Clean speech recovery with minimal artifacts

### ⚠️ OPS-SAT Radio Amateur Sample
- **Limited effectiveness** on radio noise characteristics
- Model trained on acoustic noise, not RF interference/atmospheric noise
