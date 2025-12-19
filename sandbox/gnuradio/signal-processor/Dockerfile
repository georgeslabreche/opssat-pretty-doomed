# Build lowpass-filter for OPS-SAT SEPP using pre-built GNU Radio libraries
#
# PREREQUISITES:
#   1. Setup QEMU emulation:
#      docker run --rm --privileged tonistiigi/binfmt --install arm
#
#   2. Import exp_env:
#      docker import --platform linux/arm/v7 resources/internal/exp_env.tar.gz exp_env:latest
#
#   3. Copy GNU Radio libs (run from lowpass-filter directory):
#      ./setup-libs.sh
#
# exp_env.tar.gz is Alpine 3.21.3 32-bit ARM (armv7l)

FROM exp_env:latest

# Install build dependencies (GNU Radio libs come from build-libs-armv7)
RUN apk update && apk add --no-cache \
    build-base \
    pkgconfig \
    boost-dev \
    fftw-dev \
    spdlog-dev \
    fmt-dev \
    libsndfile-dev \
    gmp-dev \
    libvolk-dev

# Copy pre-built GNU Radio libraries and headers
COPY libs-gnuradio/ /opt/gnuradio/

# Setup library paths
ENV LD_LIBRARY_PATH="/opt/gnuradio/lib:${LD_LIBRARY_PATH}"
ENV PKG_CONFIG_PATH="/opt/gnuradio/lib/pkgconfig:${PKG_CONFIG_PATH}"

# Set working directory
WORKDIR /app

# Default command
CMD ["/bin/sh"]
