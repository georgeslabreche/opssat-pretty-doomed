#!/bin/sh
# Copy sample WAV files to package input for SEPP deployment
#
# Run this before packaging:
#   ./setup-samples.sh [package-name]
#
# If no package name provided, extracts from Makefile

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SAMPLES_DIR="${SCRIPT_DIR}/../../../samples"

# Get package name from argument or extract from Makefile
if [ -n "$1" ]; then
    PACKAGE_NAME="$1"
else
    # Extract PACKAGE_VERSION from Makefile and construct package name
    VERSION=$(grep "^PACKAGE_VERSION" "${SCRIPT_DIR}/Makefile" | sed 's/.*= *//')
    PACKAGE_NAME="exp4023-signal-processor-${VERSION}"
fi

INPUT_DIR="${SCRIPT_DIR}/package/${PACKAGE_NAME}/input"

# Create input directory
mkdir -p "$INPUT_DIR"

echo "Copying sample files to input..."

# Georges samples (voice audio)
echo "  - Georges samples"
cp "$SAMPLES_DIR/georges/georges_opssat_clean.wav" "$INPUT_DIR/"
cp "$SAMPLES_DIR/georges/georges_opssat_noisy.wav" "$INPUT_DIR/"
cp "$SAMPLES_DIR/georges/georges_opssat_very_noisy.wav" "$INPUT_DIR/"

echo ""
echo "Done. Sample files copied to: package/${PACKAGE_NAME}/input/"
ls -lh "$INPUT_DIR/"
