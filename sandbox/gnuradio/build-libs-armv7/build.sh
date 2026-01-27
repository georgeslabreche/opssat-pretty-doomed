#!/bin/sh
# Build GNU Radio libraries for SEPP deployment
# Following OPS-SAT PRETTY Software Development Guide
#
# exp_env.tar.gz is Alpine 3.21.3 32-bit ARM (armv7l)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
EXP_ENV_TAR="$SCRIPT_DIR/../../../resources/internal/exp_env.tar.gz"
cd "$SCRIPT_DIR"

echo "=== Building GNU Radio libraries for SEPP ==="
echo "Target: Alpine 3.21.3 32-bit ARM (armv7l)"

# Step 1: Setup QEMU for 32-bit ARM emulation
echo "=== Setting up QEMU emulation ==="
docker run --rm --privileged tonistiigi/binfmt --install arm

# Step 2: Import exp_env.tar.gz as Docker image
echo "=== Importing exp_env.tar.gz ==="
docker rmi exp_env:latest 2>/dev/null || true
docker import --platform linux/arm/v7 "$EXP_ENV_TAR" exp_env:latest
echo "Verifying exp_env:"
docker run --rm exp_env:latest /bin/sh -c "uname -m && cat /etc/alpine-release"

# Step 3: Build the Docker image (compiles GNU Radio)
echo "=== Building Docker image (this may take 30+ minutes with emulation) ==="
docker-compose build --no-cache

# Create output directory
mkdir -p output

# Extract the built libraries
echo "=== Extracting libraries ==="
docker-compose run --rm gnuradio-builder sh -c "cp -r /output/* /export/"

# Create deployment package
echo "=== Creating deployment package ==="
mkdir -p package/gnuradio-libs-armv7
cp -r output/lib package/gnuradio-libs-armv7/
cp -r output/include package/gnuradio-libs-armv7/
cd package
COPYFILE_DISABLE=1 tar --owner=exp --group=exp -czvf gnuradio-libs-armv7.tar.gz gnuradio-libs-armv7/
cd ..

echo "=== Done ==="
echo "Libraries exported to: output/"
echo "Deployment package: package/gnuradio-libs-armv7.tar.gz"
ls -la output/lib/*.so* 2>/dev/null | head -20 || echo "Check output/lib/ for libraries"
du -sh output/
du -sh package/gnuradio-libs-armv7.tar.gz
