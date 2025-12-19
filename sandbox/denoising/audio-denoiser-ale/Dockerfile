FROM alpine:latest

# Install build tools and dependencies
RUN apk add --no-cache \
    build-base \
    libsndfile-dev \
    bash

WORKDIR /app

# Default command (keep container running)
CMD ["/bin/sh"]
