#!/usr/bin/env bash
#
# End-to-end ground preview of the on-board doom pipeline for a downlinked raw
# sc16, all the way through speech recognition.
#
#   stage 1  raw sc16 -> on-board FM audio     (tools/preview_onboard: the exact
#            capture.cpp DSP chain, with the SDR front end emulated)
#   stage 2  audio -> transcript + keyword hit (the real flight pipeline in
#            single-file mode: process_wav -> sherpa-onnx STT -> matcher)
#
# Stage 2 is the unmodified flight app (build/local/pretty-doomed -i), so the
# speech-recognition and command-detection are exactly what would run on-board.
#
# Run from the pretty-doomed/ directory inside the build container (needs the
# built app, preview_onboard, and the STT models under models/). Example:
#   docker compose run --rm pretty-doomed \
#     bash -lc 'make all preview-onboard && \
#       tools/preview_onboard_e2e.sh input/<clip>.cs16 toGround/e2e/<id>'
#
set -euo pipefail

IN_SC16=${1:?usage: preview_onboard_e2e.sh <in.sc16> <out_dir> [rec_center_hz] [rec_rate_hz]}
OUT_DIR=${2:?usage: preview_onboard_e2e.sh <in.sc16> <out_dir> [rec_center_hz] [rec_rate_hz]}
REC_CENTER=${3:-1295500000}
REC_RATE=${4:-2500000}

# Overridable paths (defaults match the in-container layout)
CONFIG=${CONFIG:-config.cfg}
VARIANTS=${VARIANTS:-variants.cfg}
DEMOS=${DEMOS:-demos}
DOOM=${DOOM:-doom-build/local/opssat-doom}
PREVIEW=${PREVIEW:-build/local/preview_onboard}
APP=${APP:-build/local/pretty-doomed}

mkdir -p "$OUT_DIR"
WAV="$OUT_DIR/onboard.wav"

echo "[1/2] on-board audio preview: $IN_SC16 -> $WAV"
"$PREVIEW" "$IN_SC16" "$WAV" "$CONFIG" "$REC_CENTER" "$REC_RATE"

echo "[2/2] flight STT + keyword matcher on $WAV"
"$APP" -i "$WAV" -c "$CONFIG" -f "$VARIANTS" -o "$OUT_DIR" -d "$DEMOS" -e "$DOOM" || true

echo
echo "=== transcription ($OUT_DIR/transcription.txt) ==="
cat "$OUT_DIR/transcription.txt" 2>/dev/null || echo "(none)"
echo "=== summary ($OUT_DIR/summary.txt) ==="
cat "$OUT_DIR/summary.txt" 2>/dev/null || echo "(none)"
