# PRETTY DOOMed build environment
#
# Unit tests:        make test      (pure C++17, no external deps)
# Integration tests: make test-dsp  (requires GNU Radio)
# Full build:        make           (requires GNU Radio + sherpa-onnx + libsndfile)

# ============================================================
# Stage 1: Build sherpa-onnx C API from source
# ============================================================
FROM debian:bookworm-slim AS sherpa-builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake git ca-certificates && \
    rm -rf /var/lib/apt/lists/*

ARG SHERPA_VERSION=1.12.21
RUN git clone --depth 1 --branch v${SHERPA_VERSION} \
    https://github.com/k2-fsa/sherpa-onnx.git /build/sherpa-onnx

WORKDIR /build/sherpa-onnx/build

RUN cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=ON \
    -DSHERPA_ONNX_ENABLE_TESTS=OFF \
    -DSHERPA_ONNX_ENABLE_PYTHON=OFF \
    -DSHERPA_ONNX_ENABLE_PORTAUDIO=OFF \
    -DSHERPA_ONNX_ENABLE_BINARY=OFF \
    -DCMAKE_INSTALL_PREFIX=/opt/sherpa-onnx

RUN make -j$(nproc)
RUN make install

# ============================================================
# Stage 2: Build environment
# ============================================================
FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    g++ \
    make \
    pkg-config \
    libsndfile1-dev \
    gnuradio-dev \
    libfmt-dev \
    libiio-dev \
    libad9361-dev \
    libfftw3-dev && \
    rm -rf /var/lib/apt/lists/*

# Copy sherpa-onnx SDK from builder stage
COPY --from=sherpa-builder /opt/sherpa-onnx /opt/sherpa-onnx
ENV LD_LIBRARY_PATH="/opt/sherpa-onnx/lib:${LD_LIBRARY_PATH}"

WORKDIR /app
