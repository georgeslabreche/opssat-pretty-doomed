# PRETTY DOOMed

Voice-command-to-DOOM pipeline for OPS-SAT PRETTY. Processes audio from amateur radio captures, applies signal conditioning, transcribes speech, detects commands, and launches DOOM.

## Pipeline

```
WAV ──> Lowpass ──> Bandpass ──> Resample ──> STT ──> Match ──> DOOM
```

Two input modes:
- **File input** (`-i`): reads a pre-recorded WAV file
- **SDR capture** (`-s`): N sequential RF captures from the AD9361 SDR, FM demodulated, then each WAV processed through the pipeline. STT model loaded once and reused across captures.

Two processing modes for SDR capture (configurable via `process_mode`):
- **sequential** (default): capture all N, then process all N
- **background**: process previous capture on a background thread while the next capture runs

All processing in memory. No intermediate files between stages (except SDR capture artifacts).

## Build

Two build targets: local (x86_64) for development and testing, SEPP (ARM32) for OPS-SAT deployment. Both build `pretty-doomed` and `opssat-doom` automatically — the DOOM source is mounted into the container via docker-compose.

### Local Build (x86_64)

```bash
# Build Docker image
docker-compose build

# Build pretty-doomed + opssat-doom
docker-compose run --rm pretty-doomed make all doom
```

This builds both binaries using Debian Bookworm x86_64 with GNU Radio and sherpa-onnx.

### Models

Download models before running — see [`models/README.md`](models/README.md). Model paths are configured in `config.cfg`.

### SEPP Build (ARM32)

Builds an ARM32 package for OPS-SAT SEPP deployment. The first build compiles GNU Radio and sherpa-onnx from source under QEMU emulation — this is slow but results are cached by Docker for subsequent builds.

Prerequisites:
- Docker and Docker Compose
- `resources/exp_env.tar.gz` (one directory up from repo root)

```bash
./build-sepp.sh
```

This script:
1. Sets up QEMU ARM32 emulation
2. Imports `exp_env.tar.gz` as Docker image
3. Builds Docker image (GNU Radio from source, cached after first run)
4. Builds sherpa-onnx C API from source inside the container
5. Builds `pretty-doomed` and `opssat-doom` (from `../doom/src`)
6. Copies model files, demo files, and bundled shared libraries
7. Creates `package/exp4023-pretty-DOOMed-v1.tar.gz`

Or step by step:

```bash
# Setup QEMU + import exp_env (one-time)
docker run --rm --privileged tonistiigi/binfmt --install arm
docker import --platform linux/arm/v7 ../resources/exp_env.tar.gz exp_env:latest

# Build Docker image (GNU Radio from source — first time is slow)
docker-compose -f docker-compose.sepp.yml build

# Build sherpa-onnx C API (first time only)
docker-compose -f docker-compose.sepp.yml run --rm pretty-doomed-sepp make build-sherpa

# Build binaries + prepare package layout (inside container)
docker-compose -f docker-compose.sepp.yml run --rm pretty-doomed-sepp make BUILDDIR=build/sepp package-prepare

# Copy input WAVs + model + demos + create tarball (outside container)
make package-input
make package-model
make package-demos
make package-tar
```

## Test

```bash
# Unit tests — pure C++17, no external deps
docker-compose run --rm pretty-doomed make test

# DSP integration tests — requires GNU Radio
docker-compose run --rm pretty-doomed make test-dsp
```

## Run

The `run` script executes the full pipeline in two phases:

1. **File input**: processes bundled WAV files (one with DOOM command, one without)
2. **SDR capture**: runs 3 sequential 20-second RF captures through the pipeline

```bash
# Full run (file input + SDR captures)
docker-compose run --rm pretty-doomed ./run
```

Each run creates a new `toGround/run-XXXXX/` directory (auto-incrementing).

### Manual execution

```bash
# File input mode
docker-compose run --rm pretty-doomed ./build/local/pretty-doomed \
    -i input/sample.wav \
    -c config.cfg \
    -f variants.cfg \
    -o output \
    -d demos \
    -e doom-build/local/opssat-doom

# SDR capture mode
docker-compose run --rm pretty-doomed ./build/local/pretty-doomed \
    -s \
    -c config.cfg \
    -f variants.cfg \
    -o output \
    -d demos \
    -e doom-build/local/opssat-doom
```

### SDR Emulator Testing

Uses the same `iio-emu` container and sample file as sdr-capture/sdr-loopback, but the pretty-doomed binary runs natively on x86_64 (Debian Bookworm). This avoids the intermittent SIGFPE crashes that affect the ARM32 sdr-capture/sdr-loopback containers running under QEMU emulation. The emulator serves IIO data over TCP and is architecture-independent.

The emulator replays a finite sample file and does not reflect config writes in readback attributes. The emulator config (`config.emu.cfg`) sets `sdr_min_readback=true` to accept the readback mismatch and uses a short capture duration (1s) to fit within the sample file.

```bash
# Start emulator, build, run SDR capture
docker-compose -f docker-compose.emu-test.yml up -d sdr-emu
docker-compose -f docker-compose.emu-test.yml run --rm pretty-doomed make clean all
docker-compose -f docker-compose.emu-test.yml run --rm pretty-doomed \
    ./build/local/pretty-doomed -s -c config.emu.cfg -f variants.cfg \
    -o toGround/emu-test -d demos -e doom-build/local/opssat-doom
docker-compose -f docker-compose.emu-test.yml down
```

### Options

| Option | Description | Required |
|--------|-------------|----------|
| `-i <file>` | Input WAV file (48 kHz mono) | Yes (unless `-s`) |
| `-s` | SDR capture mode (capture RF audio from AD9361) | No |
| `-c <file>` | Pipeline config file | Yes |
| `-f <file>` | Fuzzy match variants file | Yes |
| `-o <dir>` | Output directory | Yes |
| `-d <dir>` | DOOM demo files directory | Yes |
| `-e <file>` | DOOM binary path | Yes |
| `-v` | Verbose output | No |

## Voice Command Format

```
PRETTY, THIS IS <CALL_SIGN>, PLAY DOOM.
```

Example: "PRETTY, THIS IS GEORGES, PLAY DOOM."

## Configuration

### `config.cfg` — Pipeline parameters

```ini
# Signal Processing
lowpass_cutoff=3400
bandpass_low=300
bandpass_high=3400

# Speech-to-Text
model_encoder=models/sherpa-onnx/small/encoder-epoch-99-avg-1.int8.onnx
model_decoder=models/sherpa-onnx/small/decoder-epoch-99-avg-1.onnx
model_joiner=models/sherpa-onnx/small/joiner-epoch-99-avg-1.int8.onnx
model_tokens=models/sherpa-onnx/small/tokens.txt
decoding_method=modified_beam_search

# Detection
wake_word=PRETTY
call_signs=NIGHT,LIGHT,HEART,POWER,...
command=DOOM,PLAY DOOM
fuzzy_max_distance=1

# DOOM Frame Capture
# Per-demo: integer=snapshot, range=GIF, -1=random, list=cycling
doom_frames_e1m7-607=8000,7992-8025
doom_frames_impfight=-1
doom_frames_m1-fast=400,300,500,100
doom_frames_m1-normal=-1
doom_frames_m1-simple=-1
doom_maxframes_impfight=2030
doom_maxframes_m1-normal=1785
doom_maxframes_m1-simple=700

# Demo cycling order (GIF-producing demos first for richer first-run output)
# If not set, cycles alphabetically through all .lmp files in demos/
doom_demo_order=e1m7-607,impfight,m1-fast,m1-normal,m1-simple
```

SDR capture parameters (used with `-s` flag, all optional with defaults matching sdr-capture):

| Config Key | Default | Description |
|------------|---------|-------------|
| `sdr_frequency` | 1296000000 | RX frequency in Hz |
| `sdr_rate` | 2400000 | AD9361 sample rate in Hz |
| `sdr_decimation` | 12 | LPF decimation factor |
| `sdr_rf_bandwidth` | 200000 | AD9361 analog filter bandwidth in Hz |
| `sdr_gain` | 50 | RX gain in dB |
| `sdr_fm_deviation` | 5000 | FM deviation in Hz |
| `sdr_uri` | local: | IIO URI (local: for hardware, ip:host:port for emulator) |
| `sdr_duration` | 20 | Capture duration in seconds |
| `sdr_max_iq_mb` | 20 | Max I/Q file size in MiB |
| `sdr_audio_rate` | 16000 | Output audio sample rate in Hz |
| `sdr_timeout_multiplier` | 5 | Timeout = duration * N + 10 seconds |
| `sdr_min_readback` | false | Downgrade sample rate readback mismatch to warning |
| `sdr_enable_spectrogram` | true | Generate spectrogram BMP |
| `sdr_enable_constellation` | true | Generate I/Q constellation BMP |
| `sdr_captures` | 3 | Number of sequential SDR captures |
| `process_mode` | sequential | Processing mode: `sequential` (all captures then all processing) or `background` (process previous while capturing next) |

### `variants.cfg` — Fuzzy match variants

Known misrecognition patterns per word, derived from BPE token analysis:

```ini
PRETTY=PRETY,BRETTY,PREDDY
DOOM=DOM,DUM,DUME
NIGHT=KNIGHT,NITE,NIGH
```

## Audio Preparation

Convert a recording to the expected input format (48 kHz, mono, 16-bit):

```bash
./convert-sample.sh recording.mp3 input/sample.wav
```

## Output

When using the `run` script, each execution creates a numbered directory:

File input mode (`-i`):
```
toGround/run-00001/
├── pretty-doomed.log      # Pipeline log (timestamped, ms precision)
├── processed.wav          # Filtered audio
├── transcription.txt      # STT output
├── scores.txt             # Detection scores (exact/approximate breakdown)
├── summary.txt            # Human-readable summary
├── doom.log               # DOOM stdout/stderr (if command detected)
├── results.log            # Statdump validation (OK/ERROR per demo)
└── e1m7-607/              # DOOM demo output (one demo per run, cycling)
    ├── stats.txt          # Level statistics
    ├── frame-NNNNNN.jpg   # Snapshot (random, cycling, or fixed)
    └── frames-007992-008025.gif  # Animated GIF (if dash range configured)
```

SDR capture mode (`-s`):
```
toGround/run-00003/
├── pretty-doomed.log      # Dispatch log (capture/processing progress)
├── capture-001/           # Per-capture directory
│   ├── run.log            # Detailed capture + processing log
│   ├── capture.wav        # FM-demodulated audio
│   ├── capture.sc16       # Raw I/Q data
│   ├── spectrogram.bmp    # I/Q spectrogram
│   ├── constellation.bmp  # I/Q constellation
│   ├── processed.wav      # Filtered audio (pipeline output)
│   ├── transcription.txt  # STT output
│   ├── scores.txt
│   ├── summary.txt
│   └── e1m7-607/          # DOOM output (if command detected)
├── capture-002/
│   └── ...
└── capture-003/
    └── ...
```

### scores.txt

Machine-readable detection scores with exact and approximate match breakdown:

```
wake_word_exact=4
wake_word_exact_matches=PRETTY,PRETTY,PRETTY,PRETTY
wake_word_approx=0
wake_word_approx_matches=
command_DOOM_exact=2
command_DOOM_exact_matches=DOOM,DOOM
command_DOOM_approx=5
command_DOOM_approx_matches=DO,DO,DO,DO,DO
command_PLAY_DOOM_exact=2
command_PLAY_DOOM_exact_matches=PLAY DOOM,PLAY DOOM
command_PLAY_DOOM_approx=5
command_PLAY_DOOM_approx_matches=UPLI DO,PLAY DO,PLAY DO,PLAY DO,PLAY DO
total_points=18
total_points_exact=8
total_points_approx=10
```

## SEPP Deployment

Build the ARM32 package (see [SEPP Build](#sepp-build-arm32) above):

```bash
./build-sepp.sh
```

Creates `package/exp4023-pretty-DOOMed-v1.tar.gz` containing:

```
exp4023-pretty-DOOMed-v1/
├── run                    # SEPP entrypoint
├── pretty-doomed          # Pipeline binary (ARM32, sherpa-onnx statically linked)
├── opssat-doom            # DOOM binary (ARM32, static)
├── config.cfg
├── variants.cfg
├── libs/                  # Bundled shared libraries (GNU Radio, Boost, etc.)
├── models/                # Speech-to-text models
│   └── sherpa-onnx/
│       └── small/         # Sherpa-ONNX model (~27 MB)
├── demos/                 # doom.wad + demo files
├── input/                 # Sample WAV
└── toGround/
```

**Note:** Sherpa-ONNX and ONNX Runtime are statically linked into the `pretty-doomed` binary. The pre-built `libonnxruntime.so` targets glibc and segfaults on Alpine/musl at runtime. Static linking resolves all ONNX Runtime symbols at link time via glibc compatibility stubs, avoiding the musl/glibc ABI incompatibility. GNU Radio and other dependencies remain as bundled shared libraries in `libs/`.

On the SEPP:

```bash
tar -xzf exp4023-pretty-DOOMed-v1.tar.gz
./run
```

## Project Structure

```
pretty-doomed/
├── src/
│   ├── main.cpp               # Pipeline orchestrator
│   ├── config.cpp / .h        # Config + variants parsing
│   ├── audio_io.cpp / .h      # WAV read/write
│   ├── dsp.cpp / .h           # GNU Radio FIR filter + resampling
│   ├── transcriber.cpp / .h   # Sherpa-ONNX wrapper
│   ├── matcher.cpp / .h       # Fuzzy matching + detection
│   ├── executor.cpp / .h      # DOOM execution
│   ├── output.cpp / .h        # Summary + log output formatting
│   ├── capture.cpp / .h       # AD9361 SDR capture (RX flowgraph)
│   └── pipeline.cpp / .h      # WAV processing pipeline (DSP + STT + detect + DOOM)
├── tests/
│   ├── doctest.h              # Test framework (single header)
│   ├── test_main.cpp          # Test runner
│   ├── test_config.cpp
│   ├── test_dsp.cpp
│   ├── test_matcher.cpp
│   ├── test_executor.cpp
│   └── test_output.cpp
├── build/                     # Build output (gitignored)
│   ├── local/                 # x86_64 objects + binary
│   └── sepp/                  # ARM32 objects + binary
├── models/                    # Speech-to-text models (*.onnx gitignored)
│   └── sherpa-onnx/
│       └── small/
│           ├── encoder-epoch-99-avg-1.int8.onnx
│           ├── decoder-epoch-99-avg-1.onnx
│           ├── joiner-epoch-99-avg-1.int8.onnx
│           └── tokens.txt
├── Makefile
├── Dockerfile                 # Local x86_64 build (Debian Bookworm)
├── Dockerfile.sepp            # ARM32 SEPP build (multi-stage, GNU Radio from source)
├── docker-compose.yml         # Local dev
├── docker-compose.sepp.yml    # SEPP packaging
├── docker-compose.emu-test.yml # SDR emulator testing
├── build-sepp.sh              # One-shot SEPP build + package
├── run                        # Pipeline entrypoint
├── config.cfg                 # Pipeline + SDR config (hardware defaults)
├── config.emu.cfg             # SDR emulator config (short duration, relaxed readback)
├── variants.cfg
├── convert-sample.sh
└── docs/
    └── DESIGN.md
```

## References

- [DOOM for OPS-SAT](../doom/README.md) — Headless DOOM engine (built automatically by `make doom`)
- [Sherpa-ONNX](https://github.com/k2-fsa/sherpa-onnx)
- [GNU Radio](https://wiki.gnuradio.org/)
- [OPS-SAT](https://opssat.esa.int/)
