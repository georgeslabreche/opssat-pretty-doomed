# PocketSphinx Speech-to-Text Evaluation
# Target: Ubuntu x86_64 (build from source)
# Note: Ubuntu 24.04 required for CMake 3.25+ (PocketSphinx 5.x requirement)

FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

# Install build tools and dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    wget \
    libsndfile1-dev \
    pkg-config \
    && rm -rf /var/lib/apt/lists/*

# Build PocketSphinx from source (v5.x - modern CMake build)
# Note: Single-threaded build to avoid memory pressure in Docker
RUN git clone --depth 1 https://github.com/cmusphinx/pocketsphinx.git /opt/pocketsphinx-src \
    && cd /opt/pocketsphinx-src \
    && cmake -B build -DCMAKE_INSTALL_PREFIX=/usr/local \
    && cmake --build build -j1 \
    && cmake --install build

# Set library path
ENV LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH
ENV PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH

WORKDIR /app

# Copy source files
COPY . .

# Default command
CMD ["bash"]
