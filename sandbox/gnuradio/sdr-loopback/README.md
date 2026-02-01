# SDR Loopback Test for OPS-SAT PRETTY SEPP

Validates AD9361 SDR integration using internal loopback mode. TX routes internally to RX with no RF emission. Outputs both FM-demodulated audio and raw I/Q data. Follows [CAPTURE.md](../CAPTURE.md) guidelines: sc16 I/Q format, 200 kSPS, channelization LPF, audio bandpass, RMS normalization. Includes signal quality validation via normalized cross-correlation between input and output audio.

## Pipeline

```
input.wav -> Resample -> Scale (0.8) -> FM Mod -> AD9361 TX
  -> [loopback] -> AD9361 RX (200 kSPS)
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
docker-compose run --rm sdr-loopback make
```

## Running

### Development (Docker)

```bash
# Copy test samples
mkdir -p input
cp ../../../samples/georges/georges_opssat_clean.wav input/

# Run
docker-compose run --rm sdr-loopback sh -c "cp build/loopback_test . && ./run"
```

### On Flatsat SEPP

```bash
./run
```

## Configuration

Parameters are externalized in `config.cfg` (KEY=VALUE format). Command-line arguments override config values.

### config.cfg defaults

| Parameter | Value | Description |
|-----------|-------|-------------|
| `frequency` | 1296000000 | Center frequency (Hz) |
| `sample_rate` | 200000 | SDR sample rate (Hz) |
| `bandwidth` | 200000 | RF bandwidth (Hz) |
| `gain` | 50 | RX gain (dB) |
| `tx_attenuation` | 10.0 | TX attenuation (dB) |
| `fm_deviation` | 5000 | FM deviation (Hz) |
| `uri` | local: | IIO URI |
| `max_iq_mb` | 20 | Max I/Q file size in MiB (caps duration) |
| `audio_rate` | 16000 | Output audio sample rate (Hz) |
| `bandpass_low` | 300 | Audio bandpass low cutoff (Hz) |
| `bandpass_high` | 3400 | Audio bandpass high cutoff (Hz) |
| `lpf_cutoff` | 85000 | Channelization LPF cutoff (Hz) |
| `lpf_transition` | 15000 | Channelization LPF transition (Hz) |

`max_iq_mb` caps the capture duration derived from the input WAV length. At 200 kSPS, `max_iq_mb=20` allows up to ~26s of I/Q data. If the input WAV exceeds this, the capture is truncated to protect downlink bandwidth.

### Command Line Options (loopback_test)

| Option | Description | Default |
|--------|-------------|---------|
| `-c, --config` | Config file path | - |
| `-i, --input` | Input WAV file (required) | - |
| `-o, --output` | Output WAV file path | output.wav |
| `-u, --uri` | IIO URI | local: |
| `-f, --freq` | Frequency in Hz | 1296000000 |
| `-s, --rate` | SDR sample rate in Hz | 200000 |
| `-d, --deviation` | FM deviation in Hz | 5000 |

## Output

Each execution creates a new run directory in `toGround/`. The script processes all WAV files in `input/`, prefixing each output with `captured_`:

```
toGround/
└── run-000001/
    ├── captured_georges_opssat_clean.wav    # FM-demodulated audio (16 kHz, mono, 16-bit PCM, RMS normalized)
    ├── captured_georges_opssat_clean.sc16   # Raw I/Q data (interleaved int16, 4 bytes/sample)
    ├── loopback.log
    └── summary.txt
```

### I/Q File Format

The `.sc16` file contains raw I/Q samples at the SDR sample rate (200 kSPS):
- Format: Interleaved int16 (I, Q, I, Q, ...)
- 4 bytes per sample (2 bytes I + 2 bytes Q)
- Compatible with GNU Radio, inspectrum, baudline, etc.

## AD9361 Settings

| Parameter | Value |
|-----------|-------|
| Frequency | 1296 MHz (23cm amateur band) |
| Sample Rate | 200 kSPS |
| Bandwidth | 200 kHz |
| FM Deviation | 5 kHz (NBFM) |
| TX Attenuation | 10 dB |
| RX Gain | 50 dB (manual mode) |
| Loopback Mode | Digital (mode 1) |

## Validation

### Loopback Enable

Loopback mode must be successfully enabled and confirmed via readback before the test runs. If the loopback attribute write fails or readback doesn't match `"1"`, the test exits with a fatal error.

### Signal Quality

After capture, the test computes normalized cross-correlation between input and output WAV files (with linear interpolation for rate mismatch, ±100ms lag search):
- **PASS**: correlation >= 0.7
- **WARN**: correlation 0.3–0.7 (marginal)
- **FAIL**: correlation < 0.3 (output does not resemble input)

### Completion Condition

All three paths (TX, RX audio, RX I/Q) must reach their expected sample counts. TX stalling while RX produces noise is detected as a timeout, not a false success.

### Timeout

- Timeout = 2x expected duration + 10 seconds
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
