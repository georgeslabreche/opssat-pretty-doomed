# Use Alpine 32-bit ARM to match SEPP target environment (armv7l)
# PREREQUISITE: Setup QEMU emulation first:
#   docker run --rm --privileged tonistiigi/binfmt --install arm
FROM --platform=linux/arm/v7 alpine:3.21

# Enable community repository for GNU Radio packages
RUN echo "https://dl-cdn.alpinelinux.org/alpine/v3.21/main" > /etc/apk/repositories && \
    echo "https://dl-cdn.alpinelinux.org/alpine/v3.21/community" >> /etc/apk/repositories

# Install build dependencies, GNU Radio, and audio libraries
RUN apk update && apk add --no-cache \
    build-base \
    cmake \
    git \
    wget \
    pkgconfig \
    # GNU Radio and dependencies
    gnuradio \
    gnuradio-dev \
    boost-dev \
    fftw-dev \
    # Audio I/O
    libsndfile-dev \
    sdl2-dev

# Clone and build whisper.cpp
WORKDIR /opt
RUN set -ex && \
    git clone https://github.com/ggerganov/whisper.cpp.git && \
    cd whisper.cpp && \
    cmake -B build -DWHISPER_BUILD_EXAMPLES=ON && \
    cmake --build build --config Release -j$(nproc) && \
    # Verify build outputs exist
    ls -la build/bin/whisper-cli && \
    ls -la build/src/libwhisper.so* && \
    ls -la build/ggml/src/libggml*.so* && \
    # Install binaries and libraries
    cp build/bin/whisper-cli /usr/local/bin/ && \
    cp build/src/libwhisper.so* /usr/local/lib/ && \
    cp build/ggml/src/libggml*.so* /usr/local/lib/ && \
    # Install headers
    mkdir -p /usr/local/include/whisper && \
    cp include/whisper.h /usr/local/include/whisper/ && \
    cp ggml/include/*.h /usr/local/include/ && \
    # Verify installation
    ls -la /usr/local/lib/libwhisper* && \
    ls -la /usr/local/lib/libggml* && \
    # Update library cache
    ldconfig /usr/local/lib

# Download tiny model (for testing - 75MB)
RUN mkdir -p /opt/whisper-models && \
    cd /opt/whisper.cpp && \
    ./models/download-ggml-model.sh tiny

# Set library path
ENV LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH

# Set working directory
WORKDIR /app

# Default command
CMD ["/bin/sh"]
