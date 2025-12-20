# Sherpa-ONNX Speech-to-Text Evaluation
# Target: Ubuntu x86_64 (fast evaluation with pre-built libs)

FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# Install build tools
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    wget \
    libsndfile1-dev \
    && rm -rf /var/lib/apt/lists/*

# Download pre-built Sherpa-ONNX
ARG SHERPA_VERSION=1.10.30
RUN wget -q https://github.com/k2-fsa/sherpa-onnx/releases/download/v${SHERPA_VERSION}/sherpa-onnx-v${SHERPA_VERSION}-linux-x64-shared.tar.bz2 \
    && tar -xjf sherpa-onnx-v${SHERPA_VERSION}-linux-x64-shared.tar.bz2 -C /opt/ \
    && rm sherpa-onnx-v${SHERPA_VERSION}-linux-x64-shared.tar.bz2 \
    && ln -s /opt/sherpa-onnx-v${SHERPA_VERSION}-linux-x64-shared /opt/sherpa-onnx

# Set library path
ENV LD_LIBRARY_PATH=/opt/sherpa-onnx/lib:$LD_LIBRARY_PATH
ENV SHERPA_ONNX_DIR=/opt/sherpa-onnx

WORKDIR /app

# Copy source files
COPY . .

# Default command
CMD ["bash"]
