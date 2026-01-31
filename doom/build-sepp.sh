#!/bin/sh
# Build DOOM for OPS-SAT SEPP (ARM32)
#
# Prerequisites:
#   - Docker and Docker Compose
#   - resources/exp_env.tar.gz (one level up)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
EXP_ENV_TAR="$SCRIPT_DIR/../resources/exp_env.tar.gz"

echo "=== DOOM SEPP Build ==="

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

# Step 3: Build Docker image
echo "=== Building Docker image ==="
docker-compose -f docker-compose.sepp.yml build

# Step 4: Build DOOM + prepare package (inside ARM32 container)
echo "=== Building DOOM (static, ARM32) ==="
mkdir -p package
docker-compose -f docker-compose.sepp.yml run --rm doom-sepp sh -c "make clean && make BUILDDIR=build/sepp package-prepare"

# Step 5: Create tarball (outside container)
echo "=== Creating tarball ==="
make package-tar

echo "=== Done ==="
ls -lh package/exp4023-DOOM-*.tar.gz
