# Build GNU Radio libraries from source for OPS-SAT SEPP deployment
# Following OPS-SAT PRETTY Software Development Guide
#
# PREREQUISITE: Import exp_env and setup QEMU:
#   docker run --rm --privileged tonistiigi/binfmt --install all
#   docker import --platform linux/arm/v7 resources/internal/exp_env.tar.gz exp_env:latest
#
# exp_env.tar.gz is Alpine 3.21.3 32-bit ARM (armv7l)

FROM exp_env:latest

# exp_env already has repositories configured for Alpine 3.21

# Install build dependencies
RUN apk update && apk add --no-cache \
    # Build tools
    build-base \
    cmake \
    git \
    pkgconfig \
    python3 \
    python3-dev \
    py3-pip \
    py3-numpy \
    py3-pybind11-dev \
    # GNU Radio dependencies
    boost-dev \
    fftw-dev \
    gmp-dev \
    spdlog-dev \
    fmt-dev \
    libsndfile-dev \
    # VOLK (Vector Optimized Library of Kernels)
    libvolk-dev \
    # IIO/AD9361 dependencies (will build libiio from source)
    libusb-dev \
    libxml2-dev \
    # Optional but useful
    zeromq-dev \
    cppzmq

# Set working directory
WORKDIR /build

# Get libiio headers and create library symlink (no -dev package for armhf)
# exp_env has libiio.so.0.25 runtime, we need headers + symlink for CMake
ARG LIBIIO_VERSION=v0.25
RUN git clone --depth 1 --branch ${LIBIIO_VERSION} \
    https://github.com/analogdevicesinc/libiio.git /build/libiio && \
    cp /build/libiio/iio.h /usr/include/ && \
    ln -sf /usr/lib/libiio.so.0 /usr/lib/libiio.so

# Build libad9361-iio (required for fmcomms2 blocks in gr-iio)
ARG LIBAD9361_VERSION=v0.3
RUN git clone --depth 1 --branch ${LIBAD9361_VERSION} \
    https://github.com/analogdevicesinc/libad9361-iio.git /build/libad9361-iio

WORKDIR /build/libad9361-iio/build
RUN cmake .. \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DLIBIIO_INCLUDEDIR=/usr/include \
    -DLIBIIO_LIBRARIES=/usr/lib/libiio.so && \
    make -j$(nproc) && \
    make install

# Clone GNU Radio source
ARG GNURADIO_VERSION=v3.10.11.0
RUN git clone --depth 1 --branch ${GNURADIO_VERSION} \
    https://github.com/gnuradio/gnuradio.git /build/gnuradio

# Build GNU Radio with minimal components needed for signal processing
WORKDIR /build/gnuradio/build

RUN cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/opt/gnuradio \
    -DENABLE_DEFAULT=OFF \
    -DENABLE_GNURADIO_RUNTIME=ON \
    -DENABLE_GR_BLOCKS=ON \
    -DENABLE_GR_FFT=ON \
    -DENABLE_GR_FILTER=ON \
    -DENABLE_GR_ANALOG=ON \
    -DENABLE_GR_DIGITAL=ON \
    -DENABLE_GR_IIO=ON \
    -DENABLE_GR_CHANNELS=OFF \
    -DENABLE_GR_AUDIO=OFF \
    -DENABLE_GR_QTGUI=OFF \
    -DENABLE_GR_TRELLIS=OFF \
    -DENABLE_GR_UHD=OFF \
    -DENABLE_GR_VOCODER=OFF \
    -DENABLE_GR_SOAPY=OFF \
    -DENABLE_GR_ZEROMQ=OFF \
    -DENABLE_GR_NETWORK=OFF \
    -DENABLE_GR_PDU=OFF \
    -DENABLE_PYTHON=OFF \
    -DENABLE_TESTING=OFF \
    -DENABLE_DOXYGEN=OFF \
    -DENABLE_SPHINX=OFF \
    -DENABLE_MANPAGES=OFF

RUN make -j$(nproc)

RUN make install

# Create output directory with GNU Radio, VOLK, and libad9361 libraries
RUN mkdir -p /output/lib /output/include && \
    cp -r /opt/gnuradio/lib/* /output/lib/ && \
    cp -r /opt/gnuradio/include/* /output/include/ && \
    # Include VOLK library (required runtime dependency)
    cp -P /usr/lib/libvolk*.so* /output/lib/ && \
    cp -r /usr/include/volk /output/include/ && \
    # Include libad9361 (required for fmcomms2 blocks)
    cp -P /usr/lib/libad9361*.so* /output/lib/ && \
    cp /usr/include/ad9361.h /output/include/

# Default command - list what was built
CMD ["sh", "-c", "echo '=== GNU Radio Libraries ===' && ls -la /output/lib/ && echo '=== Size ===' && du -sh /output/"]
