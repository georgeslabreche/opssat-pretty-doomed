#!/bin/bash
set -e

# Default build config
BUILD_CONFIG="native"

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --config=*)
            BUILD_CONFIG="${1#*=}"
            shift
            ;;
        --help|-h)
            echo "Usage: $0 [--config=BUILD_TARGET]"
            echo ""
            echo "BUILD_TARGET options:"
            echo "  native          - Native architecture (default)"
            echo "  x86_64          - Native x86_64 (alias for native)"
            echo "  elinux_aarch64  - ARM64 Linux"
            echo "  elinux_armhf    - ARM 32-bit Linux"
            echo "  android_arm64   - Android ARM64"
            echo "  android_arm     - Android ARM 32-bit"
            echo "  android_x86_64  - Android x86_64"
            echo "  android_x86     - Android x86"
            echo "  ios_arm64       - iOS ARM64"
            echo "  ios_x86_64      - iOS x86_64"
            echo ""
            echo "Example: $0 --config=elinux_armhf"
            echo ""
            echo "For more cross-compilation options:"
            echo "https://ai.google.dev/edge/litert/build/arm"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

# Validate BUILD_CONFIG parameter
case "$BUILD_CONFIG" in
    "native"|"x86_64"|"elinux_aarch64"|"elinux_armhf"|"android_arm64"|"android_arm"|"android_x86_64"|"android_x86"|"ios_arm64"|"ios_x86_64")
        # Map x86_64 to native for compatibility with README
        if [ "$BUILD_CONFIG" = "x86_64" ]; then
            BUILD_CONFIG="native"
            echo "Mapping x86_64 to native build"
        fi
        echo "Building TensorFlow Lite C API for target: $BUILD_CONFIG"
        ;;
    *)
        echo "ERROR: Unsupported BUILD_CONFIG: $BUILD_CONFIG"
        echo ""
        echo "Supported configurations:"
        echo "  native          - Native architecture (default)"
        echo "  x86_64          - Native x86_64 (alias for native)"
        echo "  elinux_aarch64  - ARM64 Linux"
        echo "  elinux_armhf    - ARM 32-bit Linux"
        echo "  android_arm64   - Android ARM64"
        echo "  android_arm     - Android ARM 32-bit"
        echo "  android_x86_64  - Android x86_64"
        echo "  android_x86     - Android x86"
        echo "  ios_arm64       - iOS ARM64"
        echo "  ios_x86_64      - iOS x86_64"
        echo ""
        echo "For other Bazel configurations, see:"
        echo "https://ai.google.dev/edge/litert/build/arm"
        exit 1
        ;;
esac

# Build the TensorFlow Lite builder image with target config
docker build -f Dockerfile.tflite-builder -t tflite-builder --build-arg BUILD_CONFIG=$BUILD_CONFIG .

# Run container to extract compiled libraries
docker run --rm -v "$(pwd)/libs:/host" tflite-builder

echo "TensorFlow Lite C API built and extracted to libs/"
ls -la libs/