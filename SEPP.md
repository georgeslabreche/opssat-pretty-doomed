# Building for OPS-SAT SEPP

This document describes how to build and package applications for deployment to the OPS-SAT SEPP (Satellite Experiment Processing Platform).

## Target Environment

| Property | Value |
|----------|-------|
| OS | Alpine Linux 3.21.3 |
| Architecture | **32-bit ARM (armv7l)** |
| C Library | musl |
| Base Environment | `resources/exp_env.tar.gz` |

**Important:** The SEPP uses 32-bit ARM, not ARM64. This is critical for cross-compilation.

## Prerequisites

- Docker and Docker Compose
- QEMU for 32-bit ARM emulation (if not on native ARM32 hardware)

## Setting Up the Build Environment

### 1. Setup QEMU Emulation

On non-ARM32 systems (including Apple Silicon and x86_64), you need QEMU to emulate the 32-bit ARM environment:

```bash
docker run --rm --privileged tonistiigi/binfmt --install arm
```

This registers QEMU handlers for ARM32 binaries.

### 2. Import exp_env.tar.gz

The `exp_env.tar.gz` file contains the exact Alpine Linux filesystem used on SEPP. Import it as a Docker image:

```bash
docker import --platform linux/arm/v7 resources/exp_env.tar.gz exp_env:latest
```

Verify it works:

```bash
docker run --rm exp_env:latest /bin/sh -c "uname -m && cat /etc/alpine-release"
# Expected output:
# armv7l
# 3.21.3
```

### 3. Using exp_env in Dockerfiles

```dockerfile
# Use exp_env as base (must be imported first)
FROM exp_env:latest

# Install build tools (as per the Software Development Guide)
RUN apk add gcc musl-dev make

# Your build commands here
WORKDIR /app
COPY src/ ./src/
RUN gcc -o myapp src/myapp.c
```

For docker-compose, no special platform flag is needed since exp_env is already ARM32.

## Alternative: Using Standard Alpine

If you don't need the exact exp_env base, you can use the official Alpine image with the ARM32 platform:

```dockerfile
FROM --platform=linux/arm/v7 alpine:3.21

# Configure repositories (same as exp_env)
RUN echo "https://dl-cdn.alpinelinux.org/alpine/v3.21/main" > /etc/apk/repositories && \
    echo "https://dl-cdn.alpinelinux.org/alpine/v3.21/community" >> /etc/apk/repositories

# Install build tools
RUN apk add gcc musl-dev make
```

## Packaging for SEPP

Following the OPS-SAT PRETTY Software Development Guide:

### 1. Create Package Structure

```
my-experiment/
├── run              # Executable entrypoint script (required)
├── my-binary        # Your compiled binary
├── libs/            # Any bundled shared libraries
│   └── *.so
└── data/            # Any data files
```

### 2. Create the `run` Script

The `run` script is the entrypoint that SEPP executes:

```bash
#!/bin/sh

# Get script directory
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# Set library path for bundled libraries
export LD_LIBRARY_PATH="${SCRIPT_DIR}/libs:${LD_LIBRARY_PATH}"

# Run your application
"${SCRIPT_DIR}/my-binary"

exit $?
```

Make it executable: `chmod +x run`

### 3. Create the Deployment Package

```bash
tar --owner=exp --group=exp -czvf my-experiment.tar.gz \
    run \
    my-binary \
    libs/ \
    data/
```

**Important:**
- All files must have relative paths
- Owner must be `exp` (use `--owner=exp --group=exp`)
- The `run` script must be executable

## Runtime Environment on SEPP

When your experiment runs on SEPP:

| Property | Value |
|----------|-------|
| User | `exp` (non-root) |
| Working Directory | `$HOME` (contains your package files) |
| Entry Point | `./run` |
| Writable | Yes - changes are collected for downlink |

## Available Packages in exp_env

The base exp_env includes these packages:

```
alpine-baselayout, alpine-keys, apk-tools, avahi-libs, busybox,
c-ares, ca-certificates-bundle, dbus-libs, i2c-tools, libaio,
libcrypto3, libgcc, libiio, libiio-tools, libintl, libserialport,
libssl3, libstdc++, libusb, libxml2, mosquitto-libs, mosquitto-libs++,
musl, musl-utils, picocom, scanelf, ssl_client, xz-libs, zlib
```

Additional packages can be installed with `apk add` during build.

## Example: Building a C Application

```dockerfile
FROM exp_env:latest

# Install compiler (as per the guide)
RUN apk add gcc musl-dev make

WORKDIR /app
COPY src/ ./src/
COPY Makefile ./

# Build
RUN make

# Output will be in /app/build/
```

## Example: Building with External Libraries

If your application needs libraries not in exp_env, bundle them:

```bash
# Inside the build container, copy required libraries
ldd ./my-binary | grep "=> /" | awk '{print $3}' | xargs -I{} cp {} ./libs/
```

Then set `LD_LIBRARY_PATH` in your `run` script to find them.

## References

- `resources/internal/OPS-SAT PRETTY Software Development Guide.pdf` - Official guide
- `resources/exp_env.tar.gz` - SEPP base environment
