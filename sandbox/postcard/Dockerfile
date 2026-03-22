FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    make \
    curl \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

CMD ["/bin/bash"]
