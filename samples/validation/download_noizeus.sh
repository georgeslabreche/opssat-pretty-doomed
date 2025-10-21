#!/bin/bash
set -e

# NOIZEUS Corpus Download Script
# Downloads clean and noisy speech samples for spectral subtraction validation
# Reference: https://ecs.utdallas.edu/loizou/speech/noizeus/

CORPUS_DIR="noizeus"
BASE_URL="https://ecs.utdallas.edu/loizou/speech/noizeus"

echo "=== NOIZEUS Corpus Downloader ==="
echo "Downloading to: $CORPUS_DIR"
echo ""
echo "Note: NOIZEUS uses real-world noises from the AURORA database"
echo "Noise types: babble, car, exhibition, train (4 types × 4 SNR levels × 30 files = 480 noisy files)"
echo ""

# Create directory structure
mkdir -p "$CORPUS_DIR/clean"
mkdir -p "$CORPUS_DIR/noisy"
mkdir -p "$CORPUS_DIR/tmp"

# Download and extract clean speech files
echo "Downloading clean speech files..."
if [ ! -f "$CORPUS_DIR/clean/sp01.wav" ]; then
    curl -sS -o "$CORPUS_DIR/tmp/clean.zip" "$BASE_URL/clean.zip"
    echo "  Extracting clean.zip..."
    unzip -q -o "$CORPUS_DIR/tmp/clean.zip" -d "$CORPUS_DIR/tmp/"
    # Move files from nested directory (clean.zip contains clean/*.wav)
    mv "$CORPUS_DIR/tmp/clean"/*.wav "$CORPUS_DIR/clean/" 2>/dev/null || true
    echo "  Done: 30 clean speech files"
else
    echo "  Skipping (already downloaded)"
fi

# Noise types available in NOIZEUS (using 4 common types for validation)
NOISE_TYPES=("babble" "car" "exhibition" "train")

# SNR levels available (in dB)
SNR_LEVELS=("0" "5" "10" "15")

# Download and extract noisy speech for each noise type and SNR level
for noise in "${NOISE_TYPES[@]}"; do
    for snr in "${SNR_LEVELS[@]}"; do
        ZIP_FILE="${noise}_${snr}dB.zip"
        OUTPUT_DIR="$CORPUS_DIR/noisy/${noise}_${snr}db"

        echo ""
        echo "Downloading ${noise} noise at ${snr}dB SNR..."

        mkdir -p "$OUTPUT_DIR"

        if [ ! -f "$OUTPUT_DIR/sp01.wav" ]; then
            echo "  Downloading $ZIP_FILE..."
            curl -sS -o "$CORPUS_DIR/tmp/$ZIP_FILE" "$BASE_URL/$ZIP_FILE"
            echo "  Extracting $ZIP_FILE..."
            unzip -q -o "$CORPUS_DIR/tmp/$ZIP_FILE" -d "$CORPUS_DIR/tmp/"

            # Move and rename files from nested directory
            # Files are in tmp/{snr}dB/sp01_{noise}_sn{snr}.wav format
            # Rename them to just sp01.wav, sp02.wav, etc.
            for i in $(seq -f "%02g" 1 30); do
                src_file="$CORPUS_DIR/tmp/${snr}dB/sp${i}_${noise}_sn${snr}.wav"
                dst_file="$OUTPUT_DIR/sp${i}.wav"
                if [ -f "$src_file" ]; then
                    mv "$src_file" "$dst_file"
                fi
            done

            # Clean up extracted directory
            rm -rf "$CORPUS_DIR/tmp/${snr}dB"
            echo "  Done: 30 files"
        else
            echo "  Skipping (already downloaded)"
        fi
    done
done

# Clean up tmp directory
rm -rf "$CORPUS_DIR/tmp"

echo ""
echo "=== Download Complete ==="
echo ""
echo "Corpus structure:"
echo "  $CORPUS_DIR/clean/                    - 30 clean speech files"
echo "  $CORPUS_DIR/noisy/babble_0db/         - Babble noise at 0dB SNR (30 files)"
echo "  $CORPUS_DIR/noisy/car_5db/            - Car noise at 5dB SNR (30 files)"
echo "  $CORPUS_DIR/noisy/exhibition_10db/    - Exhibition noise at 10dB SNR (30 files)"
echo "  $CORPUS_DIR/noisy/train_15db/         - Train noise at 15dB SNR (30 files)"
echo "  ... and more combinations (4 noise types × 4 SNR levels)"
echo ""
echo "Total files: 30 clean + 480 noisy = 510 audio files"
echo ""
echo "To test with spectral subtraction:"
echo "  cd ../../sandbox/audio-denoiser-classical"
echo "  docker-compose exec spectral-denoiser sh"
echo "  ./build/spectral_subtraction samples/validation/$CORPUS_DIR/noisy/babble_5db/sp01.wav output/validation/$CORPUS_DIR/denoised/babble_5db/sp01.wav"
echo ""
echo "Or process all samples:"
echo "  ./batch_validate_spectral_subtraction.sh"
echo ""
echo "Compare results:"
echo "  Clean:    samples/validation/$CORPUS_DIR/clean/sp01.wav"
echo "  Noisy:    samples/validation/$CORPUS_DIR/noisy/babble_5db/sp01.wav"
echo "  Denoised: output/validation/$CORPUS_DIR/denoised/babble_5db/sp01.wav"
