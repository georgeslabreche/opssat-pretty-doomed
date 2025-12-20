# Vosk Speech-to-Text Evaluation
# Target: Alpine x86_64 (fast evaluation, no ARM cross-compilation)

FROM alpine:3.21

# Enable community repository for vosk-api
RUN echo "https://dl-cdn.alpinelinux.org/alpine/v3.21/main" > /etc/apk/repositories && \
    echo "https://dl-cdn.alpinelinux.org/alpine/v3.21/community" >> /etc/apk/repositories

# Install build tools and Vosk
RUN apk update && apk add --no-cache \
    build-base \
    vosk-api \
    vosk-api-dev \
    libsndfile-dev \
    wget \
    unzip

WORKDIR /app

# Copy source files
COPY . .

# Default command
CMD ["sh"]
