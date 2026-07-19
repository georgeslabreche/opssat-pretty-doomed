# Building

Two build targets: local (x86_64) for development and testing, SEPP (ARM32) for OPS-SAT deployment. Both build `pretty-doomed` and `opssat-doom` automatically. The DOOM source is mounted into the container via docker-compose.

## Local Build (x86_64)

```bash
# Build Docker image
docker-compose build

# Build pretty-doomed + opssat-doom
docker-compose run --rm pretty-doomed make all doom
```

This builds both binaries using Debian Bookworm x86_64 with GNU Radio and sherpa-onnx.

## SEPP Build (ARM32)

Builds an ARM32 package for OPS-SAT SEPP deployment. Uses pre-built GNU Radio ARM32 libraries (no GNU Radio compilation needed). Sherpa-onnx is built from source under QEMU on the first run (cached for subsequent builds).

### Prerequisites

- Docker and Docker Compose
- `resources/exp_env.tar.gz` (one directory up from repo root)
- Pre-built GNU Radio ARM32 libs at [`sandbox/gnuradio/build-libs-armv7/output/`](../sandbox/gnuradio/build-libs-armv7/) (includes GNU Radio, gr-iio, libad9361, libvolk). Build these first if they don't exist.
- SDR emulator image `iio-emu:latest` (optional, for emulator testing only)

### One-shot Build

```bash
./build-sepp.sh
```

This script:
1. Sets up QEMU ARM32 emulation
2. Imports `exp_env.tar.gz` as Docker image
3. Builds Docker image (installs Alpine deps, no GNU Radio compilation)
4. Builds sherpa-onnx C API from source inside the container (first time only, cached)
5. Builds `pretty-doomed` and `opssat-doom` (from `../doom/src`)
6. Copies model files, demo files, and bundled shared libraries
7. Creates `package/exp4023-pretty-DOOMed-v<VERSION>.tar.gz`

### Step by Step

```bash
# Setup QEMU + import exp_env (one-time)
docker run --rm --privileged tonistiigi/binfmt --install arm
docker import --platform linux/arm/v7 ../resources/exp_env.tar.gz exp_env:latest

# Build Docker image
docker-compose -f docker-compose.sepp.yml build

# Build sherpa-onnx C API (first time only)
docker-compose -f docker-compose.sepp.yml run --rm pretty-doomed-sepp make build-sherpa

# Build binaries + prepare package layout (inside container)
docker-compose -f docker-compose.sepp.yml run --rm pretty-doomed-sepp make BUILDDIR=build/sepp package-prepare

# Copy model + demos + create tarball (outside container)
make package-model
make package-demos
make package-tar
```

### Patch Package (only what changed)

When the target already has a previous version installed, build a patch package with only the files that changed and extract it over the existing installation. Decide the file list by checksum comparison against the archived previous package (rebuilds produce byte-different but functionally identical binaries, e.g. `opssat-doom` and `libiio`, so compare and test rather than assume):

```bash
# compare, per directory and per file, e.g.
diff <(cd package/archive/exp4023-pretty-DOOMed-v6 && find libs -type f | sort | xargs shasum -a 256) \
     <(cd package/exp4023-pretty-DOOMed-v7        && find libs -type f | sort | xargs shasum -a 256)

make package-patch PATCH_FROM=v6 PACKAGE_VERSION=v7 \
    PATCH_FILES="pretty-doomed run VERSION config.cfg"
```

This produces `package/exp4023-pretty-DOOMed-v6-to-v7.tar.gz`. `package-prepare` must have run first. If anything is excluded despite differing (a rebuilt-but-unchanged binary), validate the shipped binary against the deployed versions of the excluded files, per the v7 changelog.

## SEPP Deployment

The SEPP build creates `package/exp4023-pretty-DOOMed-v<VERSION>.tar.gz` containing:

```
exp4023-pretty-DOOMed-v<VERSION>/
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
├── assets/                # Postcard logos (ESA, PRETTY, DOOM)
└── toGround/
```

**Note:** Sherpa-ONNX and ONNX Runtime are statically linked into the `pretty-doomed` binary. The pre-built `libonnxruntime.so` targets glibc and segfaults on Alpine/musl at runtime. Static linking resolves all ONNX Runtime symbols at link time via glibc compatibility stubs, avoiding the musl/glibc ABI incompatibility. GNU Radio and other dependencies remain as bundled shared libraries in `libs/`.

On the SEPP:

```bash
tar -xzf exp4023-pretty-DOOMed-v<VERSION>.tar.gz
./run
```

## Models

Download models before running. See [`models/README.md`](../models/README.md) for download instructions. Model paths are configured in `config.cfg`.

## Audio Preparation

Convert a recording to the expected input format (48 kHz, mono, 16-bit):

```bash
./scripts/misc/convert-sample.sh recording.mp3 input/sample.wav
```
