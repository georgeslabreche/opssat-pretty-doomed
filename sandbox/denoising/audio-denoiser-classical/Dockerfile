FROM alpine:3.19

# Install build dependencies and audio libraries
RUN apk add --no-cache \
    build-base \
    libsndfile-dev \
    fftw-dev

# Set working directory
WORKDIR /app

# Copy source files
COPY src/ ./src/
COPY Makefile ./

# Keep container running
CMD ["tail", "-f", "/dev/null"]
