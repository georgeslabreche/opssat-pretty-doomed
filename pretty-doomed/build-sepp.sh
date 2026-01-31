#!/bin/sh
# Build PRETTY DOOMed for OPS-SAT SEPP (ARM32)
#
# Prerequisites:
#   - Docker and Docker Compose
#   - resources/exp_env.tar.gz (one level up)
#   - Model files in models/sherpa-onnx/small/ (see README.md)
#
# First build takes a long time (GNU Radio + sherpa-onnx compilation under QEMU).
# Subsequent builds are fast thanks to Docker layer caching.

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
EXP_ENV_TAR="$SCRIPT_DIR/../resources/exp_env.tar.gz"

echo "=== PRETTY DOOMed SEPP Build ==="

# Pre-flight: check model files exist before starting long build
echo "=== Checking prerequisites ==="
MISSING=""
for f in models/sherpa-onnx/small/encoder-epoch-99-avg-1.int8.onnx models/sherpa-onnx/small/decoder-epoch-99-avg-1.onnx models/sherpa-onnx/small/joiner-epoch-99-avg-1.int8.onnx models/sherpa-onnx/small/tokens.txt; do
    if [ ! -f "$SCRIPT_DIR/$f" ]; then
        MISSING="$MISSING  $f\n"
    fi
done
if [ -n "$MISSING" ]; then
    echo "ERROR: Missing model files:"
    printf "$MISSING"
    echo ""
    echo "Download them with:"
    echo "  git lfs install"
    echo "  git clone https://huggingface.co/csukuangfj/sherpa-onnx-zipformer-small-en-2023-06-26 /tmp/sherpa-model"
    echo "  mkdir -p models/sherpa-onnx/small"
    echo "  cp /tmp/sherpa-model/{encoder-epoch-99-avg-1.int8.onnx,decoder-epoch-99-avg-1.onnx,joiner-epoch-99-avg-1.int8.onnx,tokens.txt} models/sherpa-onnx/small/"
    exit 1
fi
echo "Model files: OK"

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

mkdir -p build/sepp package

# Step 4: Build sherpa-onnx static libs (first time only)
echo "=== Building sherpa-onnx static libs ==="
docker-compose -f docker-compose.sepp.yml run --rm pretty-doomed-sepp make build-sherpa

# Step 5: Build pretty-doomed + doom + prepare package
echo "=== Building pipeline + packaging ==="
docker-compose -f docker-compose.sepp.yml run --rm pretty-doomed-sepp make BUILDDIR=build/sepp clean
docker-compose -f docker-compose.sepp.yml run --rm pretty-doomed-sepp make BUILDDIR=build/sepp package-prepare

# Step 6: Copy model + demos + create tarball (outside container)
echo "=== Finalizing package ==="
make package-input
make package-model
make package-demos
make package-tar

echo "=== Done ==="
ls -lh package/exp4023-pretty-DOOMed-*.tar.gz
