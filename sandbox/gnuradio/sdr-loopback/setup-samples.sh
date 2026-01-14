#!/bin/sh
# Copy sample WAV files to input for SEPP deployment

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SAMPLES_DIR="${SCRIPT_DIR}/../../../samples"
INPUT_DIR="${SCRIPT_DIR}/package/exp4023-sdr-loopback-v1/input"

mkdir -p "$INPUT_DIR"

echo "Copying sample files to input..."
echo "  - Georges samples"
cp "$SAMPLES_DIR/georges/georges_opssat_clean.wav" "$INPUT_DIR/"

echo ""
echo "Done. Sample files copied to: package/exp4023-sdr-loopback-v1/input/"
ls -lh "$INPUT_DIR/"
