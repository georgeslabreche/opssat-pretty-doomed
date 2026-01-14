# Build sdr-capture for OPS-SAT SEPP
#
# PREREQUISITES:
#   1. Setup QEMU emulation:
#      docker run --rm --privileged tonistiigi/binfmt --install arm
#
#   2. Import exp_env:
#      docker import --platform linux/arm/v7 resources/internal/exp_env.tar.gz exp_env:latest
#
#   3. Copy GNU Radio libs:
#      ./setup-libs.sh

FROM exp_env:latest

# Install build dependencies
# libiio is in edge/testing repo
RUN echo "https://dl-cdn.alpinelinux.org/alpine/edge/testing" >> /etc/apk/repositories \
    && apk update && apk add --no-cache \
    build-base \
    cmake \
    pkgconfig \
    boost-dev \
    fftw-dev \
    spdlog-dev \
    fmt-dev \
    libsndfile-dev \
    gmp-dev \
    libvolk-dev \
    libiio-dev

# Copy pre-built GNU Radio libraries (includes gr-iio)
COPY libs-gnuradio/ /opt/gnuradio/

# Setup library paths
ENV LD_LIBRARY_PATH="/opt/gnuradio/lib:${LD_LIBRARY_PATH}"
ENV PKG_CONFIG_PATH="/opt/gnuradio/lib/pkgconfig:${PKG_CONFIG_PATH}"

WORKDIR /app

CMD ["/bin/sh"]
