# DOOM for OPS-SAT

Headless DOOM engine for OPS-SAT. Plays back demo recordings, outputs frame captures (JPEG) and level statistics. No display or audio — designed for deterministic demo playback on embedded systems.

## Build

Two build targets: local (x86_64) for development and testing, SEPP (ARM32) for OPS-SAT deployment.

### Local Build (x86_64)

```bash
docker-compose build
docker-compose run --rm doom make
```

Produces a statically linked binary at `build/local/opssat-doom`.

### SEPP Build (ARM32)

Builds an ARM32 package for OPS-SAT SEPP deployment.

Prerequisites:
- Docker and Docker Compose
- `resources/exp_env.tar.gz` (one directory up from repo root)

```bash
./build-sepp.sh
```

This script:
1. Sets up QEMU ARM32 emulation
2. Imports `exp_env.tar.gz` as Docker image
3. Builds `opssat-doom` (static, ARM32)
4. Packages binary + demo files + WAD
5. Creates `package/exp4023-DOOM-v3.tar.gz`

Or step by step:

```bash
# Setup QEMU + import exp_env (one-time)
docker run --rm --privileged tonistiigi/binfmt --install arm
docker import --platform linux/arm/v7 ../resources/exp_env.tar.gz exp_env:latest

# Build Docker image
docker-compose -f docker-compose.sepp.yml build

# Build + prepare package layout (inside container)
mkdir -p package
docker-compose -f docker-compose.sepp.yml run --rm doom-sepp make package-prepare

# Create tarball (outside container)
make package-tar
```

## Usage

```bash
./opssat-doom \
    -nosound -nomusic -nosfx \
    -runid 1 \
    -longtics \
    -iwad demos/doom.wad \
    -cdemo demos/e1m7-607 \
    -statdump output/stats.txt \
    -framedir output/
```

### Options

| Option | Description |
|--------|-------------|
| `-iwad <path>` | Path to doom.wad (required) |
| `-cdemo <path>` | Demo file to play (without .lmp extension) |
| `-statdump <path>` | Write level statistics to file |
| `-framedir <dir>` | Write frame captures (JPEG) to directory |
| `-runid <N>` | Run identifier (selects which frame to capture) |
| `-nosound` | Disable sound |
| `-nomusic` | Disable music |
| `-nosfx` | Disable sound effects |
| `-longtics` | Deterministic timing |

## Demo Files

Demo recordings in `demos/`:

| File | Description |
|------|-------------|
| `e1m7-607.lmp` | Episode 1, Mission 7 |
| `impfight.lmp` | Imp infighting |
| `m1-fast.lmp` | Mission 1, fast run |
| `m1-normal.lmp` | Mission 1, normal run |
| `m1-simple.lmp` | Mission 1, simple run |

Each demo has a corresponding `.txt` reference file for validating output.

## Test

Run all demos and validate statistics against reference files:

```bash
docker-compose run --rm doom sh run
```

Output:

```
toGround/run-00001/
├── doom.log               # DOOM stdout/stderr
├── results.log            # Statdump validation (OK/ERROR per demo)
└── runs/
    ├── e1m7-607/
    │   ├── stats.txt
    │   └── frame-001920.jpg
    ├── impfight/
    │   ├── stats.txt
    │   └── frame-000780.jpg
    └── ...
```

## SEPP Deployment

```bash
./build-sepp.sh
```

Creates `package/exp4023-DOOM-v3.tar.gz` containing:

```
exp4023-DOOM-v3/
├── run                # Entrypoint (runs demos, validates stats)
├── opssat-doom        # DOOM binary (ARM32, static)
├── demos/             # doom.wad + demo .lmp + reference .txt files
└── toGround/
```

## Project Structure

```
doom/
├── src/                       # DOOM source code (C)
│   ├── Makefile               # Source-level build
│   └── *.c / *.h
├── build/                     # Build output (gitignored)
│   ├── local/                 # x86_64 objects + binary
│   └── sepp/                  # ARM32 objects + binary
├── demos/                     # WAD + demo files
├── Makefile                   # Top-level build + packaging
├── Dockerfile                 # Local x86_64 build
├── Dockerfile.sepp            # ARM32 SEPP build
├── docker-compose.yml         # Local dev
├── docker-compose.sepp.yml    # SEPP packaging
├── build-sepp.sh              # One-shot SEPP build + package
└── run                        # Test / SEPP entrypoint
```
