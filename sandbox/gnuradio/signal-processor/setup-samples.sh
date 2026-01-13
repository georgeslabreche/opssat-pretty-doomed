#!/bin/sh
# Copy sample WAV files to io/input for SEPP deployment
#
# Run this before packaging:
#   ./setup-samples.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SAMPLES_DIR="${SCRIPT_DIR}/../../../samples"
INPUT_DIR="${SCRIPT_DIR}/package/exp4023-signal-processor-v1/input"

# Create input directory
mkdir -p "$INPUT_DIR"

echo "Copying sample files to input..."

# Georges samples (voice audio)
echo "  - Georges samples"
cp "$SAMPLES_DIR/georges/georges_opssat_clean.wav" "$INPUT_DIR/"
cp "$SAMPLES_DIR/georges/georges_opssat_noisy.wav" "$INPUT_DIR/"
cp "$SAMPLES_DIR/georges/georges_opssat_very_noisy.wav" "$INPUT_DIR/"

echo ""
echo "Done. Sample files copied to: package/exp4023-signal-processor-v1/input/"
ls -lh "$INPUT_DIR/"
