#!/bin/sh
# Copy sample WAV files to io/input for SEPP deployment
#
# Run this before packaging:
#   ./setup-samples.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SAMPLES_DIR="${SCRIPT_DIR}/../../../samples"
IO_INPUT="${SCRIPT_DIR}/package/signal-processor/io/input"

# Create input directory
mkdir -p "$IO_INPUT"

echo "Copying sample files to io/input..."

# Georges samples (voice audio)
echo "  - Georges samples"
cp "$SAMPLES_DIR/georges/georges_opssat_clean.wav" "$IO_INPUT/"
cp "$SAMPLES_DIR/georges/georges_opssat_noisy.wav" "$IO_INPUT/"
cp "$SAMPLES_DIR/georges/georges_opssat_very_noisy.wav" "$IO_INPUT/"

echo ""
echo "Done. Sample files copied to: package/signal-processor/io/input/"
ls -lh "$IO_INPUT/"
