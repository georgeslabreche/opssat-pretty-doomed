# GNU Radio Signal Processor for OPS-SAT SEPP

> **Status:** Validated on EM and spacecraft. Confirmed GNU Radio DSP filters (lowpass, bandpass, squelch) run correctly on ARM32 hardware. The signal processing logic has since been integrated into the [`pretty-doomed`](../../../pretty-doomed/) pipeline (via `dsp.cpp` and `capture.cpp`).

A GNU Radio C++ application demonstrating a signal processing pipeline for voice audio on the OPS-SAT SEPP spacecraft.

## Pipeline

```
WAV File Source -> Lowpass Filter -> Bandpass Filter -> Power Squelch -> WAV File Sink
```

**Voice-optimized settings:**
- Lowpass: 3400 Hz cutoff
- Bandpass: 300-3400 Hz (voice frequency range)
- Power Squelch: -50 dB threshold (gates noise during silence)

## Prerequisites

- Docker and Docker Compose
- Pre-built GNU Radio libraries in `../build-libs-armv7/output/`

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

Copy pre-built libraries from `build-libs-armv7`:

```bash
./setup-libs.sh
```

This copies `lib/` and `include/` from `../build-libs-armv7/output/` to `libs-gnuradio/`.

## Building

```bash
docker-compose build
docker-compose run --rm signal-processor make
```

## Running

There are two ways to run the signal processor:

### Option 1: Make Targets (Development)

Quick single-file tests during development. Outputs to `io/output/`.

```bash
# Process individual samples
docker-compose run --rm signal-processor make run-clean
docker-compose run --rm signal-processor make run-noisy
docker-compose run --rm signal-processor make run-very-noisy

# Process all samples
docker-compose run --rm signal-processor make test
```

**Output:** `io/output/processed_*.wav`

### Option 2: Run Script (SEPP Deployment)

The `run` script is the SEPP entrypoint. It processes all WAV files in `io/input/` and saves artifacts to `toGround/` for downlink. Each run auto-increments a run ID.

```bash
# Run the SEPP entrypoint script
docker-compose run --rm signal-processor sh -c "cp build/signal_processor . && ./run"
```

**Output structure:**
```
toGround/
├── run-000001/
│   ├── processed_*.wav         # Filtered audio files
│   ├── signal_processor.log    # Detailed processing log
│   └── summary.txt             # Results summary
├── run-000002/
│   └── ...
└── run-000003/
    └── ...
```

Each invocation of `./run` creates a new `run-NNNNNN/` directory with incrementing ID.

## Usage

```
./build/signal_processor [options]
  -i, --input       Input WAV file path (required)
  -o, --output      Output WAV file path (default: output.wav)
  -l, --lowpass     Lowpass cutoff frequency in Hz (default: 3400)
  -b, --bandlow     Bandpass low frequency in Hz (default: 300)
  -B, --bandhigh    Bandpass high frequency in Hz (default: 3400)
  -t, --threshold   Squelch threshold in dB (default: -50)
  -h, --help        Show help message
```

## Creating Deployment Package

Create a SEPP-ready package with bundled libraries:

```bash
# Step 1: Inside container - prepare binary and libraries
docker-compose run --rm signal-processor make package-prepare

# Step 2: Outside container - copy sample WAV files (runs ./setup-samples.sh)
make package-samples

# Step 3: Outside container - create tarball with exp:exp ownership
make package-tar
```

The `setup-samples.sh` script copies the georges voice samples to `package/exp4023-signal-processor-v1/input/`.

This creates `package/exp4023-signal-processor-v1.tar.gz` containing:

```
exp4023-signal-processor-v1/
├── signal_processor        # ARM32 binary
├── run                     # SEPP entrypoint script
├── libs/                   # GNU Radio + VOLK libraries
│   ├── libgnuradio-*.so*
│   └── libvolk*.so*
└── input/                  # Input WAV files
    └── georges_*.wav
```

The `run` script processes all WAV files in `input/` and writes outputs to `toGround/run-NNNNNN/` for downlink.

All files are owned by `exp:exp` as required by SEPP.

## Output Format

The output is a mono WAV file (first channel of stereo input) with 16-bit PCM samples at the same sample rate as the input.
