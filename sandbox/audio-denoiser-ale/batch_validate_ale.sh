#!/bin/sh
#
# Batch validation script for ALE denoiser on NOIZEUS corpus
#

set -e

# Configuration
NOISE_TYPES="babble car exhibition train"
SNR_LEVELS="0 5 10 15"
SAMPLE_DIR="samples/validation/noizeus/noisy"
OUTPUT_DIR="output/validation/noizeus/ale"
ALE_BIN="build/ale_denoiser"

# Validate executable exists
if [ ! -f "$ALE_BIN" ]; then
    echo "Error: ALE denoiser binary not found. Run 'make' first."
    exit 1
fi

# Validate NOIZEUS samples exist
if [ ! -d "$SAMPLE_DIR" ]; then
    echo "Error: NOIZEUS samples not found at $SAMPLE_DIR"
    echo "Run samples/validation/download_noizeus.sh first"
    exit 1
fi

# Create output directories
mkdir -p "$OUTPUT_DIR"

echo "=== ALE Batch Validation on NOIZEUS Corpus ==="
echo ""

total=0
processed=0

# Count total files
for noise in $NOISE_TYPES; do
    for snr in $SNR_LEVELS; do
        input_dir="$SAMPLE_DIR/${noise}_${snr}db"
        if [ -d "$input_dir" ]; then
            count=$(find "$input_dir" -name "*.wav" | wc -l)
            total=$((total + count))
        fi
    done
done

echo "Total files to process: $total"
echo ""

# Process all combinations
for noise in $NOISE_TYPES; do
    for snr in $SNR_LEVELS; do
        input_dir="$SAMPLE_DIR/${noise}_${snr}db"
        output_subdir="$OUTPUT_DIR/${noise}_${snr}db"

        if [ ! -d "$input_dir" ]; then
            echo "Skipping $noise at ${snr}dB SNR (directory not found)"
            continue
        fi

        mkdir -p "$output_subdir"

        echo "Processing: $noise at ${snr}dB SNR"

        for input_file in "$input_dir"/*.wav; do
            if [ ! -f "$input_file" ]; then
                continue
            fi

            filename=$(basename "$input_file")
            output_file="$output_subdir/$filename"

            # Process with ALE
            ./"$ALE_BIN" "$input_file" "$output_file" > /dev/null 2>&1

            processed=$((processed + 1))

            # Progress indicator
            if [ $((processed % 10)) -eq 0 ]; then
                echo "  Progress: $processed/$total files"
            fi
        done
    done
done

echo ""
echo "=== Validation Complete ==="
echo "Processed: $processed files"
echo "Output directory: $OUTPUT_DIR"
