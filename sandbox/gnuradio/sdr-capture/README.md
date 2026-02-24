# SDR Capture for OPS-SAT PRETTY SEPP

RF capture for live signal reception. Runs N sequential captures (configurable via `captures` in `config.cfg`). Each capture produces FM-demodulated audio and raw I/Q data from AD9361 SDR. Follows [CAPTURE.md](../CAPTURE.md) guidelines: sc16 I/Q format, 2.4 MSPS hardware with 12x software decimation to 200 kSPS effective, channelization LPF, audio bandpass, RMS normalization.

## Pipeline

```
AD9361 RX (1296 MHz, 2.4 MSPS)
  -> Decimating LPF (85 kHz cutoff, 12x -> 200 kSPS)
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
| `sdr_rate` | 2400000 | AD9361 hardware sample rate (Hz) |
| `decimation` | 12 | LPF decimation factor (effective rate = sdr_rate / decimation) |
| `rf_bandwidth` | 200000 | AD9361 analog RF bandwidth (Hz) |
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
| `rx_channels` | 1 | Number of IIO RX channels (1 or 2; emulator needs 2) |

`sdr_rate` must be within the AD9361 hardware range (2,083,000 – 61,440,000 Hz). `rf_bandwidth` must be within the AD9361 analog filter range (200,000 – 56,000,000 Hz). `sdr_rate` must be evenly divisible by `decimation`. The effective sample rate (= `sdr_rate` / `decimation`) is the rate at which I/Q data is written to disk and audio is demodulated.

`decimation=1` is technically valid (no decimation — the LPF runs but does not downsample). At `sdr_rate=2400000` with `decimation=1`, the effective rate would be 2.4 MSPS, producing ~192 MB of I/Q data for 20 seconds. The `max_iq_mb` budget cap truncates the capture duration to protect downlink bandwidth, so this is self-correcting but wasteful.

`duration` and `max_iq_mb` act as independent limits per capture — the shorter of the two wins. At 200 kSPS effective rate (2.4 MSPS / 12), `max_iq_mb=20` allows up to ~26s of I/Q data. With `duration=20`, the duration is the active constraint. If `duration` is raised above ~26s, the budget cap truncates it to protect downlink bandwidth. `captures` controls how many sequential captures to run (default 30 x 20s = 10 minutes total).

### Run Script Options

The `run` script reads `captures` and `duration` from `config.cfg` and accepts overrides:

| Option | Description | Default |
|--------|-------------|---------|
| `--captures N` | Number of sequential captures | `captures` from config.cfg |
| `--duration N` | Capture duration per capture in seconds | `duration` from config.cfg |

### Command Line Options (capture_loop)

| Option | Description | Default |
|--------|-------------|---------|
| `-c, --config` | Config file path | - |
| `-o, --output` | Output WAV file path | capture.wav |
| `-d, --duration` | Capture duration in seconds | 20 |
| `-u, --uri` | IIO URI | local: |
| `-f, --freq` | Frequency in Hz | 1296000000 |
| `-g, --gain` | RX gain in dB | 50 |
| `-e, --deviation` | FM deviation in Hz | 5000 |
| `--no-readback` | Skip IIO readback validation (for emulator testing) | false |

## Output

Each execution creates a single run directory in `toGround/`, with a subdirectory per capture:

```
toGround/
└── run-000001/
    ├── run.log
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

The `.sc16` file contains raw I/Q samples at the effective rate (200 kSPS after decimation):
- Format: Interleaved int16 (I, Q, I, Q, ...)
- 4 bytes per sample (2 bytes I + 2 bytes Q)
- 20 seconds at 200 kSPS = ~16 MB
- Compatible with GNU Radio, inspectrum, baudline, etc.

## AD9361 Settings

| Parameter | Value |
|-----------|-------|
| Frequency | 1296 MHz (23cm amateur band) |
| Hardware Sample Rate | 2.4 MSPS |
| Decimation | 12x (200 kSPS effective) |
| RF Bandwidth | 200 kHz |
| FM Deviation | 5 kHz (NBFM) |
| RX Gain | 50 dB (manual mode) |

## Packaging for SEPP

```bash
# Inside container
docker-compose run --rm sdr-capture make package-prepare

# Outside container
make package-tar
```

Creates `package/exp4023-sdr-capture-v2.tar.gz` (~9 MB).

### Bundled Libraries

The package includes all required dependencies:
- GNU Radio (runtime, blocks, filter, fft, pmt, analog, iio)
- AD9361 + VOLK
- Boost (filesystem, program_options, thread)
- FFTW3, libsndfile, spdlog, fmt, gmp, orc
- Audio codecs (FLAC, Vorbis, Opus, MP3)

## SDR Emulator Testing

An SDR emulator is available for testing the IIO data path without real hardware. The emulator acts as a libiio network context that replays a recorded sample file. It is file-readback only: setting changes (frequency, sample rate, bandwidth) are accepted but do not affect the data returned.

The emulator Docker image (`sdr_emu.tar`) and sample file are not included in this repository due to size. Request them from the OPS-SAT mission control team.

### Emulator Setup

1. Load the emulator image:
   ```bash
   docker load -i sdr_emu.tar
   ```

2. Copy the sample file:
   ```bash
   mkdir -p emu-samples
   cp sdr_20210519_101920_1176450000_38400000_12.cs16 emu-samples/
   ```

3. Start the emulator and run a capture:
   ```bash
   docker-compose -f docker-compose.emu-test.yml up -d sdr-emu
   docker-compose -f docker-compose.emu-test.yml run --rm sdr-capture make
   docker-compose -f docker-compose.emu-test.yml run --rm sdr-capture \
     sh -c "cp build/capture_loop . && ./capture_loop \
       --config config.emu.cfg --output toGround/emu-test.wav --no-readback"
   docker-compose -f docker-compose.emu-test.yml down
   ```

### Emulator Config (config.emu.cfg)

The emulator config matches the sample file parameters (38.4 MSPS, 2 RX channels, GPS L5 @ 1176.45 MHz) with decimation=192 to produce the same 200 kSPS effective rate as production:

| Parameter | Emulator | Production |
|-----------|----------|------------|
| `sdr_rate` | 38400000 | 2400000 |
| `decimation` | 192 | 12 |
| `rx_channels` | 2 | 1 |
| `frequency` | 1176450000 | 1296000000 |
| `rf_bandwidth` | 38400000 | 200000 |
| Effective rate | 200000 | 200000 |

### Limitations

- The emulator ignores all SDR setting changes — it always streams the recorded sample file at its original parameters. The `--no-readback` flag is required to skip IIO readback validation.
- GNU Radio flowgraphs crash under QEMU ARM emulation (VOLK SIMD issues). Full end-to-end emulator testing requires a native ARM environment or real hardware on the flatsat.
