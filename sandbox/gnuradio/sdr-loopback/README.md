# SDR Loopback Test for OPS-SAT SEPP

Validates AD9361 SDR integration using internal loopback mode. TX routes internally to RX with no RF emission. Outputs both FM-demodulated audio and raw I/Q data.

## Pipeline

```
                                                    ┌─> I/Q File (.cf32)
input.wav -> FM Mod -> AD9361 TX -> [loopback] -> AD9361 RX ─┤
                                                    └─> FM Demod -> WAV
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
docker import --platform linux/arm/v7 resources/internal/exp_env.tar.gz exp_env:latest
```

### 3. Copy GNU Radio Libraries

```bash
./setup-libs.sh
```

## Building

```bash
docker-compose build
docker-compose run --rm sdr-loopback make
```

## Running

### Development (Docker)

```bash
# Copy test samples
mkdir -p io/input
cp ../../../samples/georges/georges_opssat_clean.wav io/input/

# Run
docker-compose run --rm sdr-loopback sh -c "cp build/loopback_test . && ./run"
```

### On Flatsat SEPP

```bash
./run
```

## Command Line Options (loopback_test)

| Option | Description | Default |
|--------|-------------|---------|
| `-i, --input` | Input WAV file (required) | - |
| `-o, --output` | Output WAV file path | output.wav |
| `-u, --uri` | IIO URI | local: |
| `-f, --freq` | Frequency in Hz | 1296000000 |
| `-s, --rate` | SDR sample rate in Hz | 528000 |
| `-d, --deviation` | FM deviation in Hz | 5000 |

## Output

Each run creates a new directory in `toGround/`:

```
toGround/
├── run-000001/
│   ├── captured.wav      # FM-demodulated audio (original sample rate, mono, 16-bit PCM)
│   ├── captured.cf32     # Raw I/Q data (complex float32, 8 bytes/sample)
│   ├── loopback.log
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
| Bandwidth | 200 kHz |
| FM Deviation | 5 kHz (NBFM) |
| TX Attenuation | 10 dB |
| RX Gain | 50 dB (manual mode) |
| Loopback Mode | Digital (mode 1) |

## Timeout Handling

The loopback test includes automatic timeout protection:
- Timeout = 2x expected duration + 10 seconds
- If RX doesn't receive enough samples, the test exits with a warning
- Helps detect loopback failures or SDR connection issues

## Packaging for SEPP

```bash
# Inside container
docker-compose run --rm sdr-loopback make package-prepare

# Outside container
make package-samples
make package-tar
```

Creates `package/exp4023-sdr-loopback-v1.tar.gz` (~10 MB).

### Bundled Libraries

The package includes all required dependencies:
- GNU Radio (runtime, blocks, filter, fft, pmt, analog, iio)
- AD9361 + VOLK
- Boost (filesystem, program_options, thread)
- FFTW3, libsndfile, spdlog, fmt, gmp, orc
- Audio codecs (FLAC, Vorbis, Opus, MP3)

### Included Sample Files

- `input/georges_opssat_clean.wav` - Test voice sample ("PRETTY, Play DOOM")
