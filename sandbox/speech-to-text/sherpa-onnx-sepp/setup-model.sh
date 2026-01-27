#!/bin/sh
# Copy Sherpa-ONNX Small model (int8 quantized) to package
#
# Uses int8 quantized models for smaller size:
#   - encoder-epoch-99-avg-1.int8.onnx (~26 MB)
#   - decoder-epoch-99-avg-1.onnx (~2 MB) - no int8 version
#   - joiner-epoch-99-avg-1.int8.onnx (~260 KB)
#   - tokens.txt (~5 KB)
#
# Total: ~28 MB

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
MODEL_SRC="${SCRIPT_DIR}/../sherpa-onnx/models/sherpa-onnx-zipformer-small-en-2023-06-26"

# Get package name from Makefile
VERSION=$(grep "^PACKAGE_VERSION" "${SCRIPT_DIR}/Makefile" | sed 's/.*= *//')
PACKAGE_NAME="exp4023-sherpa-onnx-${VERSION}"

MODEL_DST="${SCRIPT_DIR}/package/${PACKAGE_NAME}/model"

if [ ! -d "$MODEL_SRC" ]; then
    echo "Error: Model not found at $MODEL_SRC"
    echo "Please download the model first:"
    echo "  cd ../sherpa-onnx && make download-model-small"
    exit 1
fi

mkdir -p "$MODEL_DST"

echo "Copying int8 quantized model files..."

# Copy int8 encoder (26 MB)
cp "$MODEL_SRC/encoder-epoch-99-avg-1.int8.onnx" "$MODEL_DST/"
echo "  - encoder-epoch-99-avg-1.int8.onnx"

# Copy decoder (no int8 available, use full - 2 MB)
cp "$MODEL_SRC/decoder-epoch-99-avg-1.onnx" "$MODEL_DST/"
echo "  - decoder-epoch-99-avg-1.onnx"

# Copy int8 joiner (260 KB)
cp "$MODEL_SRC/joiner-epoch-99-avg-1.int8.onnx" "$MODEL_DST/"
echo "  - joiner-epoch-99-avg-1.int8.onnx"

# Copy tokens
cp "$MODEL_SRC/tokens.txt" "$MODEL_DST/"
echo "  - tokens.txt"

echo ""
echo "Done. Model files copied to: package/${PACKAGE_NAME}/model/"
du -sh "$MODEL_DST"
ls -lh "$MODEL_DST/"
