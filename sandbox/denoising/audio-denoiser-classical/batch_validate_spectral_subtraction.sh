#!/bin/sh
set -e

# Batch validation script for NOIZEUS corpus
# Processes all noisy samples through spectral subtraction

VALIDATION_DIR="samples/validation/noizeus"
OUTPUT_DIR="output/validation/noizeus/spectral_subtraction"
BINARY="./build/spectral_subtraction"

echo "=== Batch Validation - Spectral Subtraction ==="
echo ""

# Check if NOIZEUS corpus exists
if [ ! -d "$VALIDATION_DIR" ]; then
    echo "ERROR: NOIZEUS corpus not found at $VALIDATION_DIR"
    echo "Please run: cd ../../samples/validation && ./download_noizeus.sh"
    exit 1
fi

# Check if binary exists
if [ ! -f "$BINARY" ]; then
    echo "ERROR: Binary not found. Building..."
    make
fi

# Create output directory structure
mkdir -p "$OUTPUT_DIR"

# Noise types and SNR levels (space-separated for POSIX sh compatibility)
# NOIZEUS uses real-world noises from AURORA database
NOISE_TYPES="babble car exhibition train"
SNR_LEVELS="0 5 10 15"

# Track statistics
total_files=0
processed_files=0
failed_files=0

# Process each noise type and SNR level
for noise in $NOISE_TYPES; do
    for snr in $SNR_LEVELS; do
        INPUT_DIR="$VALIDATION_DIR/noisy/${noise}_${snr}db"
        NOISE_OUTPUT_DIR="$OUTPUT_DIR/${noise}_${snr}db"

        if [ ! -d "$INPUT_DIR" ]; then
            echo "Skipping ${noise}_${snr}db (not found)"
            continue
        fi

        echo "Processing ${noise} noise at ${snr}dB SNR..."
        mkdir -p "$NOISE_OUTPUT_DIR"

        # Process all files in this category
        for input_file in "$INPUT_DIR"/*.wav; do
            if [ -f "$input_file" ]; then
                filename=$(basename "$input_file")
                output_file="$NOISE_OUTPUT_DIR/$filename"

                total_files=$((total_files + 1))

                # Skip if already processed
                if [ -f "$output_file" ]; then
                    echo "  Skipping $filename (already exists)"
                    processed_files=$((processed_files + 1))
                    continue
                fi

                echo "  Processing $filename..."
                if $BINARY "$input_file" "$output_file"; then
                    processed_files=$((processed_files + 1))
                else
                    echo "  FAILED: $filename"
                    failed_files=$((failed_files + 1))
                fi
            fi
        done
    done
done

echo ""
echo "=== Validation Complete ==="
echo "Total files: $total_files"
echo "Processed: $processed_files"
echo "Failed: $failed_files"
echo ""
echo "Results saved to: $OUTPUT_DIR"
echo ""
echo "Output structure:"
echo "  $OUTPUT_DIR/babble_5db/sp01.wav    - Denoised sample"
echo "  $OUTPUT_DIR/car_10db/sp01.wav      - Denoised sample"
echo "  ..."
echo ""
echo "Compare with:"
echo "  Clean:    $VALIDATION_DIR/clean/sp01.wav"
echo "  Noisy:    $VALIDATION_DIR/noisy/babble_5db/sp01.wav"
echo "  Denoised: $OUTPUT_DIR/babble_5db/sp01.wav"
