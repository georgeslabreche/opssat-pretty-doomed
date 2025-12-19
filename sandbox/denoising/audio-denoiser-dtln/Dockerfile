FROM debian:bookworm-slim

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    wget \
    unzip \
    libsndfile1-dev \
    libfftw3-dev \
    pkg-config \
    && rm -rf /var/lib/apt/lists/*

# Copy precompiled TensorFlow Lite C API
COPY libs/ /opt/

# Set library path
ENV LD_LIBRARY_PATH=/opt/lib:$LD_LIBRARY_PATH

# Create working directory
WORKDIR /workspace

# Copy project files
COPY . .

# Set default command
CMD ["sh"]