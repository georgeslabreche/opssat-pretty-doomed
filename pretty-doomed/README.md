# PRETTY DOOMed

Voice-command-to-DOOM pipeline for OPS-SAT PRETTY. Processes audio from amateur radio captures, applies signal conditioning, transcribes speech, detects commands, and launches DOOM.

## Pipeline

```
WAV ──> Lowpass ──> Bandpass ──> Resample ──> STT ──> Match ──> DOOM
```

All processing in memory. No intermediate files between stages.

## Prerequisites

The DOOM binary must be built before running the pipeline. See the [DOOM README](../doom/README.md) for build instructions.

For local development, the DOOM source is mounted into the container and built with `make doom` (see below).

## Build

Two build targets: local (x86_64) for development and testing, SEPP (ARM32) for OPS-SAT deployment.

### Local Build (x86_64)

```bash
# Build Docker image
docker-compose build

# Build DOOM binary (required first time)
docker-compose run --rm pretty-doomed make doom

# Build pretty_doomed binary
docker-compose run --rm pretty-doomed make
```

This builds the `pretty_doomed` binary using Debian Bookworm x86_64 with GNU Radio and sherpa-onnx.

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
5. Builds `pretty_doomed` and `opssat-doom` (from `../doom/src`)
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
docker-compose -f docker-compose.sepp.yml run --rm pretty-doomed-sepp make package-prepare

# Copy model + demos + create tarball (outside container)
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

The `run` script manages output directories and executes the full pipeline:

```bash
# Single file (default: input/sample.wav)
docker-compose run --rm pretty-doomed sh run

# Single file (explicit)
docker-compose run --rm pretty-doomed sh run input/sample.wav

# All WAV files in a directory (one run per file)
docker-compose run --rm pretty-doomed sh run input/
```

Each run creates a new `toGround/run-XXXXX/` directory (auto-incrementing) containing all output. When given a directory, each WAV file gets its own numbered run.

### Manual execution

```bash
docker-compose run --rm pretty-doomed ./build/pretty_doomed \
    -i input/sample.wav \
    -c config.cfg \
    -f variants.cfg \
    -o output \
    -d demos \
    -e doom-src/bin/opssat-doom \
    -v
```

### Options

| Option | Description | Required |
|--------|-------------|----------|
| `-i <file>` | Input WAV file (48 kHz mono) | Yes |
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
model_encoder=model/encoder-epoch-99-avg-1.int8.onnx
model_decoder=model/decoder-epoch-99-avg-1.onnx
model_joiner=model/joiner-epoch-99-avg-1.int8.onnx
model_tokens=model/tokens.txt
decoding_method=modified_beam_search

# Detection
wake_word=PRETTY
call_signs=NIGHT,LIGHT,HEART,POWER,...
command=DOOM,PLAY DOOM
fuzzy_max_distance=2
```

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

```
toGround/run-00001/
├── pretty_doomed.log      # Pipeline log (timestamped)
├── denoised.wav           # Filtered audio
├── transcription.txt      # STT output
├── scores.txt             # Detection scores (exact/approximate breakdown)
├── summary.txt            # Human-readable summary
└── runs/                  # DOOM demo output (if command detected)
    ├── doom.log           # DOOM stdout/stderr
    ├── results.log        # Statdump validation (OK/ERROR per demo)
    ├── e1m7-607/
    │   ├── stats.txt      # Level statistics
    │   └── frame-001920.jpg
    ├── impfight/
    │   ├── stats.txt
    │   └── frame-000780.jpg
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
├── pretty_doomed          # Pipeline binary (ARM32)
├── opssat-doom            # DOOM binary (ARM32, static)
├── config.cfg
├── variants.cfg
├── libs/                  # Bundled shared libraries
├── model/                 # Sherpa-ONNX model (~27 MB)
├── demos/                 # doom.wad + demo files
├── input/                 # Sample WAV
└── toGround/
```

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
│   └── executor.cpp / .h      # DOOM execution
├── tests/
│   ├── doctest.h              # Test framework (single header)
│   ├── test_main.cpp          # Test runner
│   ├── test_config.cpp
│   ├── test_dsp.cpp
│   └── test_matcher.cpp
├── model/                     # Sherpa-ONNX model files (*.onnx gitignored)
│   ├── encoder-epoch-99-avg-1.int8.onnx
│   ├── decoder-epoch-99-avg-1.onnx
│   ├── joiner-epoch-99-avg-1.int8.onnx
│   └── tokens.txt
├── Makefile
├── Dockerfile                 # Local x86_64 build (Debian Bookworm)
├── Dockerfile.sepp            # ARM32 SEPP build (multi-stage, GNU Radio from source)
├── docker-compose.yml         # Local dev
├── docker-compose.sepp.yml    # SEPP packaging
├── build-sepp.sh              # One-shot SEPP build + package
├── run                        # Pipeline entrypoint
├── config.cfg
├── variants.cfg
├── convert-sample.sh
└── docs/
    └── DESIGN.md
```

## Models

Uses [sherpa-onnx-zipformer-small-en-2023-06-26](https://huggingface.co/csukuangfj/sherpa-onnx-zipformer-small-en-2023-06-26) (int8 quantized, ~27 MB). Supports `greedy_search` and `modified_beam_search` decoding methods.

The `.onnx` files are gitignored due to size. Download and copy the required files:

```bash
git lfs install
git clone https://huggingface.co/csukuangfj/sherpa-onnx-zipformer-small-en-2023-06-26 /tmp/sherpa-model
cp /tmp/sherpa-model/{encoder-epoch-99-avg-1.int8.onnx,decoder-epoch-99-avg-1.onnx,joiner-epoch-99-avg-1.int8.onnx,tokens.txt} model/
```

Model paths are configured in `config.cfg`.

## References

- [DOOM for OPS-SAT](../doom/README.md) — Headless DOOM engine (must be built first)
- [Sherpa-ONNX](https://github.com/k2-fsa/sherpa-onnx)
- [GNU Radio](https://wiki.gnuradio.org/)
- [OPS-SAT](https://opssat.esa.int/)
