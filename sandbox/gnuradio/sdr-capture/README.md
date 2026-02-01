# SDR Capture for OPS-SAT PRETTY SEPP

RF capture for live signal reception. Runs N sequential captures (configurable via `captures` in `config.cfg`). Each capture produces FM-demodulated audio and raw I/Q data from AD9361 SDR. Follows [CAPTURE.md](../CAPTURE.md) guidelines: sc16 I/Q format, 200 kSPS, channelization LPF, audio bandpass, RMS normalization.

## Pipeline

```
AD9361 RX (1296 MHz, 200 kSPS)
  -> Complex LPF (85 kHz cutoff)
    +-> head -> sc16 conversion -> File (.sc16)
    +-> FM Demod -> Resample (16 kHz) -> Bandpass (300-3400 Hz)
        -> head -> WAV -> RMS Normalize (-20 dBFS)
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
# Run captures (count and duration from config.cfg)
docker-compose run --rm sdr-capture sh -c "cp build/capture_loop . && ./run"

# Override number of captures
docker-compose run --rm sdr-capture sh -c "cp build/capture_loop . && ./run --captures 1"

# Override duration per capture
docker-compose run --rm sdr-capture sh -c "cp build/capture_loop . && ./run --duration 10"
```

### On Flatsat SEPP

```bash
# Run captures (count and duration from config.cfg)
./run

# Override number of captures
./run --captures 5

# Override duration per capture
./run --duration 30
```

## Configuration

Parameters are externalized in `config.cfg` (KEY=VALUE format). Command-line arguments override config values.

### config.cfg defaults

| Parameter | Value | Description |
|-----------|-------|-------------|
| `frequency` | 1296000000 | Center frequency (Hz) |
| `sample_rate` | 200000 | SDR sample rate (Hz) |
| `gain` | 50 | RX gain (dB) |
| `fm_deviation` | 5000 | FM deviation (Hz) |
| `uri` | local: | IIO URI |
| `duration` | 20 | Capture duration per capture (seconds) |
| `captures` | 30 | Number of sequential captures (30 x 20s = 10 min) |
| `max_iq_mb` | 20 | Max I/Q file size in MiB (caps duration) |
| `audio_rate` | 16000 | Output audio sample rate (Hz) |
| `bandpass_low` | 300 | Audio bandpass low cutoff (Hz) |
| `bandpass_high` | 3400 | Audio bandpass high cutoff (Hz) |
| `lpf_cutoff` | 85000 | Channelization LPF cutoff (Hz) |
| `lpf_transition` | 15000 | Channelization LPF transition (Hz) |

`duration` and `max_iq_mb` act as independent limits per capture — the shorter of the two wins. At 200 kSPS, `max_iq_mb=20` allows up to ~26s of I/Q data. With `duration=20`, the duration is the active constraint. If `duration` is raised above ~26s, the budget cap truncates it to protect downlink bandwidth. `captures` controls how many sequential captures to run (default 30 x 20s = 10 minutes total).

### Command Line Options (capture_loop)

| Option | Description | Default |
|--------|-------------|---------|
| `-c, --config` | Config file path | - |
| `-o, --output` | Output WAV file path | capture.wav |
| `-d, --duration` | Capture duration in seconds | 20 |
| `-u, --uri` | IIO URI | local: |
| `-f, --freq` | Frequency in Hz | 1296000000 |
| `-s, --rate` | SDR sample rate in Hz | 200000 |
| `-g, --gain` | RX gain in dB | 50 |
| `-e, --deviation` | FM deviation in Hz | 5000 |

## Output

Each execution creates a single run directory in `toGround/`, with a subdirectory per capture:

```
toGround/
└── run-000001/
    ├── capture-001/
    │   ├── capture.wav      # FM-demodulated audio (16 kHz, mono, 16-bit PCM, RMS normalized)
    │   ├── capture.sc16     # Raw I/Q data (interleaved int16, 4 bytes/sample)
    │   └── capture.log
    ├── capture-002/
    │   ├── capture.wav
    │   ├── capture.sc16
    │   └── capture.log
    ├── ...
    └── summary.txt
```

The number of `capture-NNN/` subdirectories matches the `captures` config value (or `--captures` override).

### I/Q File Format

The `.sc16` file contains raw I/Q samples at the SDR sample rate (200 kSPS):
- Format: Interleaved int16 (I, Q, I, Q, ...)
- 4 bytes per sample (2 bytes I + 2 bytes Q)
- 20 seconds at 200 kSPS = ~16 MB
- Compatible with GNU Radio, inspectrum, baudline, etc.

## AD9361 Settings

| Parameter | Value |
|-----------|-------|
| Frequency | 1296 MHz (23cm amateur band) |
| Sample Rate | 200 kSPS |
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
