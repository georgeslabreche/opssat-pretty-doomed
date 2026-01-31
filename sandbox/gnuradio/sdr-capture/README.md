# SDR Capture Loop for OPS-SAT SEPP

Continuous RF capture loop for live signal reception. Captures FM-demodulated audio and raw I/Q data from AD9361 SDR.

## Pipeline

```
AD9361 RX (1296 MHz) ─┬─> I/Q File (.cf32)
                      └─> FM Demod -> Resample (48 kHz) -> WAV
```

## Prerequisites

- Docker and Docker Compose
- Pre-built GNU Radio + gr-iio libraries in `../build-libs-armv7/output/`
- QEMU ARM emulation for local testing

## Setup

### 1. Setup QEMU Emulation (one-time)

```bash
docker run --rm --privileged tonistiigi/binfmt --install arm
```

### 2. Import exp_env (one-time)

```bash
docker import --platform linux/arm/v7 resources/exp_env.tar.gz exp_env:latest
```

### 3. Copy GNU Radio Libraries

```bash
./setup-libs.sh
```

## Building

```bash
docker-compose build
docker-compose run --rm sdr-capture make
```

## Running

### Development (Docker)

```bash
# Single 10-second capture
docker-compose run --rm sdr-capture sh -c "cp build/capture_loop . && ./run --once"

# Continuous capture loop (5-second intervals)
docker-compose run --rm sdr-capture sh -c "cp build/capture_loop . && ./run --duration 5"
```

### On Flatsat SEPP

```bash
# Single capture
./run --once

# Continuous loop (default: 10 seconds each)
./run

# Custom duration
./run --duration 30
```

## Command Line Options (capture_loop)

| Option | Description | Default |
|--------|-------------|---------|
| `-o, --output` | Output WAV file path | capture.wav |
| `-d, --duration` | Capture duration in seconds | 10 |
| `-u, --uri` | IIO URI | local: |
| `-f, --freq` | Frequency in Hz | 1296000000 |
| `-s, --rate` | SDR sample rate in Hz | 528000 |
| `-g, --gain` | RX gain in dB | 50 |
| `-e, --deviation` | FM deviation in Hz | 5000 |

## Output

Each run creates a new directory in `toGround/`:

```
toGround/
├── run-000001/
│   ├── capture.wav      # FM-demodulated audio (48 kHz, mono, 16-bit PCM)
│   ├── capture.cf32     # Raw I/Q data (complex float32, 8 bytes/sample)
│   ├── capture.log
│   └── summary.txt
├── run-000002/
└── ...
```

### I/Q File Format

The `.cf32` file contains raw I/Q samples at the SDR sample rate (default 528 kHz):
- Format: Interleaved float32 (I, Q, I, Q, ...)
- 8 bytes per sample (4 bytes I + 4 bytes Q)
- Compatible with GNU Radio, inspectrum, baudline, etc.

## AD9361 Settings

| Parameter | Value |
|-----------|-------|
| Frequency | 1296 MHz (23cm amateur band) |
| Sample Rate | 528 kHz |
| FM Deviation | 5 kHz (NBFM) |
| RX Gain | 50 dB (manual mode) |

## Packaging for SEPP

```bash
# Inside container
docker-compose run --rm sdr-capture make package-prepare

# Outside container
make package-tar
```

Creates `package/exp4023-sdr-capture-v1.tar.gz` (~9 MB).

### Bundled Libraries

The package includes all required dependencies:
- GNU Radio (runtime, blocks, filter, fft, pmt, analog, iio)
- AD9361 + VOLK
- Boost (filesystem, program_options, thread)
- FFTW3, libsndfile, spdlog, fmt, gmp, orc
- Audio codecs (FLAC, Vorbis, Opus, MP3)
