#!/bin/sh
# Copy pre-built GNU Radio libraries from build-libs-armv7
#
# Run this before building the Docker image:
#   ./setup-libs.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
LIBS_SRC="${SCRIPT_DIR}/../build-libs-armv7/output"
LIBS_DST="${SCRIPT_DIR}/libs-gnuradio"

# Check if source exists
if [ ! -d "$LIBS_SRC" ]; then
    echo "ERROR: GNU Radio libraries not found at: $LIBS_SRC"
    echo ""
    echo "Please build them first:"
    echo "  cd ../build-libs-armv7"
    echo "  ./build.sh"
    exit 1
fi

# Clean and copy
echo "Copying GNU Radio libraries from build-libs-armv7/output..."
rm -rf "$LIBS_DST"
mkdir -p "$LIBS_DST"
cp -r "$LIBS_SRC/lib" "$LIBS_DST/"
cp -r "$LIBS_SRC/include" "$LIBS_DST/"

echo "Done. Libraries copied to: libs-gnuradio/"
echo ""
echo "Contents:"
ls -la "$LIBS_DST/lib/"*.so 2>/dev/null | head -10
echo ""
echo "Now you can build the Docker image:"
echo "  docker-compose build"
