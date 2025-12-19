# GNU Radio Signal Processor for OPS-SAT SEPP

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
docker import --platform linux/arm/v7 resources/internal/exp_env.tar.gz exp_env:latest
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

Run individual samples or all at once. No logging, just processes the files.

```bash
# Process individual samples
docker-compose run --rm signal-processor make run-clean
docker-compose run --rm signal-processor make run-noisy
docker-compose run --rm signal-processor make run-very-noisy

# Process all samples
docker-compose run --rm signal-processor make test
```

### Option 2: Run Script (SEPP Deployment)

The `run` script is the SEPP entrypoint. It processes all WAV files in `io/input/` and creates logs.

```bash
# Copy samples to io/input
mkdir -p io/input
cp ../../../samples/georges/*.wav io/input/

# Run the SEPP entrypoint script
docker-compose run --rm signal-processor sh -c "cp build/signal_processor . && ./run"
```

**Output files:** `io/output/processed_*.wav`

**Logs (run script only):**
- `io/output/signal_processor.log` - detailed processing log
- `io/output/summary.txt` - summary of results

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

The `setup-samples.sh` script copies the georges voice samples to `package/signal-processor/io/input/`.

This creates `package/signal-processor.tar.gz` containing:

```
signal-processor/
├── signal_processor        # ARM32 binary
├── run                     # SEPP entrypoint script
├── libs/                   # GNU Radio + VOLK libraries
│   ├── libgnuradio-*.so*
│   └── libvolk*.so*
└── io/
    ├── input/              # Input WAV files
    │   └── georges_*.wav
    └── output/             # Output directory (for downlink)
```

The `run` script processes all WAV files in `io/input/` and writes processed outputs to `io/output/`.

All files are owned by `exp:exp` as required by SEPP.

## Output Format

The output is a mono WAV file (first channel of stereo input) with 16-bit PCM samples at the same sample rate as the input.
