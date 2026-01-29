#!/bin/sh
# Build PRETTY DOOMed for OPS-SAT SEPP (ARM32)
#
# Prerequisites:
#   - Docker and Docker Compose
#   - resources/exp_env.tar.gz (one level up)
#
# First build takes a long time (GNU Radio + sherpa-onnx compilation under QEMU).
# Subsequent builds are fast thanks to Docker layer caching.

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
EXP_ENV_TAR="$SCRIPT_DIR/../resources/exp_env.tar.gz"

echo "=== PRETTY DOOMed SEPP Build ==="

# Step 1: Setup QEMU for ARM32 emulation
echo "=== Setting up QEMU emulation ==="
docker run --rm --privileged tonistiigi/binfmt --install arm

# Step 2: Import exp_env
echo "=== Importing exp_env ==="
if [ ! -f "$EXP_ENV_TAR" ]; then
    echo "ERROR: exp_env.tar.gz not found at: $EXP_ENV_TAR"
    exit 1
fi
docker rmi exp_env:latest 2>/dev/null || true
docker import --platform linux/arm/v7 "$EXP_ENV_TAR" exp_env:latest

# Step 3: Build Docker image (GNU Radio from source — cached after first build)
echo "=== Building Docker image (GNU Radio from source) ==="
docker-compose -f docker-compose.sepp.yml build

mkdir -p build package

# Step 4: Build sherpa-onnx C API (first time only)
echo "=== Building sherpa-onnx C API ==="
docker-compose -f docker-compose.sepp.yml run --rm pretty-doomed-sepp make build-sherpa

# Step 5: Build pretty-doomed + doom + prepare package
echo "=== Building pipeline + packaging ==="
docker-compose -f docker-compose.sepp.yml run --rm pretty-doomed-sepp make package-prepare

# Step 6: Copy model + demos + create tarball (outside container)
echo "=== Finalizing package ==="
make package-model
make package-demos
make package-tar

echo "=== Done ==="
ls -lh package/exp4023-pretty-DOOMed-v1.tar.gz
