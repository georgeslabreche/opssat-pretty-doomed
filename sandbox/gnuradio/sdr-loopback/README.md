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
- Shared headers in `common/` (repo root) — mounted into Docker at `/app/common`
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
| `rate_tolerance` | 10 | Max Hz offset for sample rate readback before fatal (AD9361 PLL quantization) |
| `timeout_multiplier` | 5 | Timeout = duration * N + 10 seconds (default 5 for ARM CPU headroom) |
| `iio_buffer_size` | 32768 | IIO DMA buffer size in samples per channel (tune to reduce TX/RX contention) |
| `single_core` | false | Pin process to CPU 0 (diagnose threading issues) |
| `min_readback` | false | Downgrade sample rate readback mismatch to warning (for emulator) |
| `enable_spectrogram` | true | Generate spectrogram BMP from captured I/Q data |
| `enable_constellation` | true | Generate I/Q constellation BMP (detects Q channel dropout) |
| `tx_mode` | default | TX execution mode: `default` (simultaneous), `staggered` (TX DMA active but sends silence first), `cyclic` (DMA loop) |
| `tx_startup_delay` | 2 | Seconds of TX silence before audio begins (staggered mode only) |

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
| `--single-core` | Pin process to CPU 0 (diagnose threading issues) | false |

### Run Script

The `run` script takes no arguments. It executes a series of diagnostic runs defined in the `RUNS` variable at the top of the script. Each run processes all `.wav` files in `input/` with a different configuration.

The `RUNS` variable uses a `label:override1,override2,...` format. Overrides are appended to the base `config.cfg` (last value wins). If `RUNS` is empty, a single run with the default config is executed.

Default diagnostic schedule (investigating Q channel dropout):
1. **default** — TX and RX start simultaneously (baseline, reproduces v5/v6 Q dropout)
2. **staggered** — TX DMA active but sends 5s silence before audio (tests TX content vs TX DMA activity)
3. **cyclic** — TX loops a single DMA buffer instead of continuous streaming (tests DMA contention)

## Output

Each run creates its own directory in `toGround/` with a copy of the effective config, log, summary, and output files:

```
toGround/
├── experiment.log                                         # Top-level experiment log
├── run-000001/                                            # default
│   ├── config.cfg                                         # Effective config (base + overrides)
│   ├── run.log
│   ├── summary.txt
│   ├── captured_georges_opssat_clean.wav                  # FM-demodulated audio (16 kHz, mono, 16-bit PCM, RMS normalized)
│   ├── captured_georges_opssat_clean.sc16                 # Raw I/Q data (interleaved int16, 4 bytes/sample)
│   ├── spectrogram.bmp                                    # Spectrogram thumbnail (1024x256, ~768 KB)
│   └── constellation.bmp                                  # I/Q constellation scatter (256x256, ~192 KB)
├── run-000002/                                            # staggered
│   └── ...
└── run-000003/                                            # cyclic
    └── ...
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

The RX I/Q head block reaching its expected sample count is the completion trigger. The audio path may fall slightly short due to filter group delay (resampler + bandpass) when the I/Q head completes — this is normal and does not affect the output. A 500ms grace period after I/Q completion lets the scheduler drain in-flight buffer items before stopping the flowgraph. TX stalling while RX produces noise is detected as a timeout, not a false success.

### Timeout

- Timeout = `timeout_multiplier` * expected duration + 10 seconds (default multiplier: 5)
- Helps detect loopback failures or SDR connection issues
- The default 5x multiplier provides headroom for the EM's ARM CPU, which processes RX at ~40-80k SPS vs the expected 200k SPS
- Configurable via `timeout_multiplier` in config.cfg

## Packaging for SEPP

```bash
# Inside container
docker-compose run --rm sdr-loopback make package-prepare

# Outside container
make package-samples
make package-tar
```

Creates `package/exp4023-sdr-loopback-v8.tar.gz`. For emulator testing, uncomment the `uri` and `min_readback` lines in `config.cfg` (or pass `--uri` and `--min-readback` on the command line).

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

Note: the RX/TX LO frequency readback is always a warning, never fatal. The AD9361 PLL quantizes to the nearest achievable frequency based on its reference clock dividers, so a small offset (typically a few Hz) is normal and has no practical impact on reception. The sample rate readback also allows a small tolerance (`rate_tolerance`, default ±10 Hz) for the same reason — the AD9361 may quantize the sample rate by a few Hz (e.g. 2,399,999 vs 2,400,000). Offsets beyond the tolerance are fatal when strict (default on EM/FlatSat) or a warning when `min_readback=true` (emulator).

### Emulator Limitations

- The emulator has no TX support, so it cannot be used for loopback testing (TX -> RX). The loopback test requires real AD9361 hardware on the flatsat.
- The emulator accepts SDR setting writes but does not reflect them in readback attributes (e.g. `sampling_frequency`). The `min_readback` config key (or `--min-readback` CLI flag) downgrades the sample rate check to a warning.
- QEMU user-mode ARM emulation (ARM32 on ARM64) can produce intermittent SIGFPE crashes unrelated to the experiment code. Full end-to-end testing requires native ARM hardware on the flatsat.

See the [sdr-capture README](../sdr-capture/README.md#sdr-emulator-testing) for emulator setup instructions.

### IIO Config Test (No GNU Radio)

A standalone test verifies IIO config write/readback without GNU Radio, avoiding QEMU SIGFPE issues:

```bash
docker-compose -f docker-compose.emu-test.yml up -d sdr-emu
docker-compose -f docker-compose.emu-test.yml run --rm sdr-loopback make test-iio
docker-compose -f docker-compose.emu-test.yml down
```

This builds and runs `test_iio_config` from `common/test/`. It writes RX config (frequency, sample rate, bandwidth, gain) to the emulator and confirms readback matches. See [common/README.md](../../../common/README.md#test_iio_config) for details.

## Known Issues

### Q Channel Dropout (v5)

Running `device_source` (RX) and `device_sink` (TX) simultaneously causes Q channel data loss on the EM. The v5 sc16 file shows 59% of Q samples are exactly zero while I is healthy (0.02% zeros). The dropout worsens over time: Q alternates between live and dead in the first ~10 seconds, then dies completely for the remainder of the capture. This does not occur in sdr-capture (RX only, no `device_sink`). Suspected cause: IIO DMA buffer contention between TX and RX streaming. Under investigation via configurable `iio_buffer_size` and diagnostic runs.

### FPGA Register Crash

See [sdr-capture Known Issues](../sdr-capture/README.md#known-issues) for the `fmcomms2_source_fc32` / `fmcomms2_sink_fc32` FPGA register crash and the `device_source` / `device_sink` fix. The same issue and fix apply to this loopback test (both source and sink).
