# SDR Loopback Test for OPS-SAT PRETTY SEPP

Validates AD9361 SDR integration using internal loopback mode. TX routes internally to RX with no RF emission. Outputs both FM-demodulated audio and raw I/Q data. Follows [CAPTURE.md](../CAPTURE.md) guidelines: sc16 I/Q format, 2.4 MSPS hardware with 12x software decimation to 200 kSPS effective, channelization LPF, audio bandpass, RMS normalization. Includes signal quality validation via normalized cross-correlation between input and output audio.

## Pipeline

```
input.wav -> Resample -> Scale (0.8) -> FM Mod -> AD9361 TX (2.4 MSPS)
  -> [loopback] -> AD9361 RX (2.4 MSPS)
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
docker-compose run --rm sdr-loopback make
```

## Running

Input WAV files must be mono, 16-bit PCM. Place them in the `input/` directory.

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
| `sdr_rate` | 2400000 | AD9361 hardware sample rate (Hz) |
| `decimation` | 12 | LPF decimation factor (effective rate = sdr_rate / decimation) |
| `rf_bandwidth` | 200000 | AD9361 analog RF bandwidth (Hz) |
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
| `min_readback` | false | Downgrade sample rate readback mismatch to warning (for emulator) |

`sdr_rate` must be within the AD9361 hardware range (2,083,000 – 61,440,000 Hz). `rf_bandwidth` must be within the AD9361 analog filter range (200,000 – 56,000,000 Hz). `sdr_rate` must be evenly divisible by `decimation`. The effective sample rate (= `sdr_rate` / `decimation`) is the rate at which I/Q data is written to disk and audio is demodulated.

`decimation=1` is technically valid (no decimation — the LPF runs but does not downsample). At `sdr_rate=2400000` with `decimation=1`, the effective rate would be 2.4 MSPS, producing ~192 MB of I/Q data for 20 seconds. The `max_iq_mb` budget cap truncates the capture duration to protect downlink bandwidth, so this is self-correcting but wasteful.

`max_iq_mb` caps the capture duration derived from the input WAV length. At 200 kSPS effective rate (2.4 MSPS / 12), `max_iq_mb=20` allows up to ~26s of I/Q data. If the input WAV exceeds this, the capture is truncated to protect downlink bandwidth.

### Command Line Options (loopback_test)

| Option | Description | Default |
|--------|-------------|---------|
| `-c, --config` | Config file path | - |
| `-i, --input` | Input WAV file (required) | - |
| `-o, --output` | Output WAV file path | output.wav |
| `-u, --uri` | IIO URI | local: |
| `-f, --freq` | Frequency in Hz | 1296000000 |
| `-d, --deviation` | FM deviation in Hz | 5000 |
| `--min-readback` | Minimal readback: downgrade sample rate check to warning (emulator) | false |

## Output

Each execution creates a new run directory in `toGround/`. The script processes all WAV files in `input/`, prefixing each output with `captured_`:

```
toGround/
└── run-000001/
    ├── run.log
    ├── captured_georges_opssat_clean.wav    # FM-demodulated audio (16 kHz, mono, 16-bit PCM, RMS normalized)
    ├── captured_georges_opssat_clean.sc16   # Raw I/Q data (interleaved int16, 4 bytes/sample)
    └── summary.txt
```

### I/Q File Format

The `.sc16` file contains raw I/Q samples at the effective rate (200 kSPS after decimation):
- Format: Interleaved int16 (I, Q, I, Q, ...)
- 4 bytes per sample (2 bytes I + 2 bytes Q)
- Compatible with GNU Radio, inspectrum, baudline, etc.

## AD9361 Settings

| Parameter | Value |
|-----------|-------|
| Frequency | 1296 MHz (23cm amateur band) |
| Hardware Sample Rate | 2.4 MSPS |
| Decimation | 12x (200 kSPS effective) |
| RF Bandwidth | 200 kHz |
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

Creates `package/exp4023-sdr-loopback-v3.tar.gz`. For emulator testing, uncomment the `uri` and `min_readback` lines in `config.cfg` (or pass `--uri` and `--min-readback` on the command line).

### Bundled Libraries

The package includes all required dependencies:
- GNU Radio (runtime, blocks, filter, fft, pmt, analog, iio)
- AD9361 + VOLK
- Boost (filesystem, program_options, thread)
- FFTW3, libsndfile, spdlog, fmt, gmp, orc
- Audio codecs (FLAC, Vorbis, Opus, MP3)

### Included Sample Files

- `input/georges_opssat_clean.wav` - Test voice sample ("PRETTY, Play DOOM")

## SDR Emulator Testing

An SDR emulator is available for testing the IIO data path without real hardware. The emulator acts as a libiio network context that replays a recorded sample file. It is file-readback only: setting changes (frequency, sample rate, bandwidth) are accepted but do not affect the data returned.

The emulator Docker image (`sdr_emu.tar`) and sample file are not included in this repository due to size. Request them from the OPS-SAT mission control team.

For emulator testing, uncomment the `uri` and `min_readback` lines at the bottom of `config.cfg` (or pass `--uri` and `--min-readback` on the command line). The emulator accepts parameter writes but does not update the `sampling_frequency` readback attribute; `--min-readback` downgrades the sample rate check from fatal to a warning while all other readbacks still run.

### Emulator Limitations

- The emulator has no TX support, so it cannot be used for loopback testing (TX -> RX). The loopback test requires real AD9361 hardware on the flatsat.
- The emulator accepts SDR setting writes but does not reflect them in readback attributes (e.g. `sampling_frequency`). The `min_readback` config key (or `--min-readback` CLI flag) downgrades the sample rate check to a warning.
- QEMU user-mode ARM emulation (ARM32 on ARM64) can produce intermittent SIGFPE crashes unrelated to the experiment code. Full end-to-end testing requires native ARM hardware on the flatsat.

See the [sdr-capture README](../sdr-capture/README.md#sdr-emulator-testing) for emulator setup instructions.

## Known Issues

See [sdr-capture Known Issues](../sdr-capture/README.md#known-issues) for the `fmcomms2_source_fc32` / `fmcomms2_sink_fc32` FPGA register crash and the `device_source` / `device_sink` fix. The same issue and fix apply to this loopback test (both source and sink).
