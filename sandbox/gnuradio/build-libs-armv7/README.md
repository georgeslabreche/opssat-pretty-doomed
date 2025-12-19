# GNU Radio Libraries Build for OPS-SAT SEPP

Builds GNU Radio libraries from source for deployment to the SEPP satellite.

**Target:** Alpine 3.21.3 32-bit ARM (armv7l) - matches `exp_env.tar.gz`

## What Gets Built

A GNU Radio build with signal processing and SDR components:
- `gnuradio-runtime` - Core runtime
- `gnuradio-blocks` - Basic blocks (file sources/sinks, etc.)
- `gnuradio-fft` - FFT operations
- `gnuradio-filter` - Filter blocks and design tools
- `gnuradio-analog` - Signal sources (sine, noise, etc.)
- `gnuradio-digital` - Digital modulation/demodulation (PSK, FSK, OFDM, etc.)
- `gnuradio-iio` - Industrial I/O support for AD9361/PlutoSDR
- `libvolk` - Vector Optimized Library of Kernels (copied from Alpine's `libvolk-dev`)

Disabled components (to reduce size): Python bindings, Qt GUI, UHD, audio, network, etc.

## Prerequisites

- Docker and Docker Compose

## Building

```bash
./build.sh
```

This will:
1. Setup QEMU emulation for 32-bit ARM
2. Import `exp_env.tar.gz` as Docker base image
3. Build GNU Radio from source inside exp_env
4. Export libraries to `output/`
5. Create `gnuradio-libs-armv7.tar.gz` for SEPP deployment

Build time: ~30-60 minutes (QEMU emulation is slower than native)

## Output

```
output/
├── lib/
│   ├── libgnuradio-runtime.so*
│   ├── libgnuradio-blocks.so*
│   ├── libgnuradio-fft.so*
│   ├── libgnuradio-filter.so*
│   ├── libgnuradio-analog.so*
│   ├── libgnuradio-digital.so*
│   ├── libgnuradio-iio.so*
│   ├── libgnuradio-pmt.so*
│   └── libvolk.so*
└── include/
    ├── gnuradio/
    │   └── *.h
    └── volk/
        └── *.h

package/
├── gnuradio-libs-armv7/          # Untarred package
│   ├── lib/
│   └── include/
└── gnuradio-libs-armv7.tar.gz    # Ready for SEPP deployment
```

## Deploying to SEPP

Three deployment options depending on your access level and requirements:

### Option 1: System-Wide Installation

Requires root/admin access. Libraries and headers are installed globally.

```sh
# Extract to system paths
tar -xzf gnuradio-libs-armv7.tar.gz -C /usr/local

# Update linker cache
ldconfig
```

- Libraries in `/usr/local/lib/` - no `LD_LIBRARY_PATH` needed
- Headers in `/usr/local/include/gnuradio/` - compile with `-I/usr/local/include`

### Option 2: Shared Location

Install once to a shared directory accessible by all experiments.

```sh
# Extract to shared location (one-time setup)
mkdir -p /opt/gnuradio
tar -xzf gnuradio-libs-armv7.tar.gz -C /opt/gnuradio
```

- Libraries in `/opt/gnuradio/lib/`
- Headers in `/opt/gnuradio/include/gnuradio/`

Compile your application with:
```sh
g++ -I/opt/gnuradio/include -L/opt/gnuradio/lib -lgnuradio-runtime -lgnuradio-blocks ... -o my-app src/my-app.cpp
```

In each experiment's `run` script:
```sh
#!/bin/sh
export LD_LIBRARY_PATH="/opt/gnuradio/lib:$LD_LIBRARY_PATH"
exec ./my-app "$@"
```

### Option 3: Bundled with Application

Bundle libraries with each experiment. Larger package size but fully self-contained with no external dependencies.

**Compile on build machine** (using headers from `output/include/`):
```sh
g++ -I output/include -L output/lib -lgnuradio-runtime -lgnuradio-blocks ... -o my-app src/my-app.cpp
```

**Package structure for deployment:**
```
my-experiment/
├── run                    # Entry point script
├── my-app                 # Your compiled binary
└── libs/                  # Bundled GNU Radio libraries
    ├── libgnuradio-runtime.so.3.10.11.0
    ├── libgnuradio-blocks.so.3.10.11.0
    ├── libgnuradio-filter.so.3.10.11.0
    └── ...
```

In your `run` script:
```sh
#!/bin/sh
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
export LD_LIBRARY_PATH="${SCRIPT_DIR}/libs:${LD_LIBRARY_PATH}"
exec "${SCRIPT_DIR}/my-app" "$@"
```

Create the deployment package:
```sh
# Copy required libraries (headers not needed at runtime)
mkdir -p my-experiment/libs
cp output/lib/*.so* my-experiment/libs/

# Package for SEPP
tar --owner=exp --group=exp -czvf my-experiment.tar.gz -C my-experiment .
```

## Customizing the Build

Edit the `Dockerfile` CMake options to enable/disable components:
- `ENABLE_GR_DIGITAL=ON` - Digital modulation/demodulation
- `ENABLE_GR_CHANNELS=ON` - Channel models
- `ENABLE_GR_AUDIO=ON` - Audio I/O
- etc.

See [GNU Radio build options](https://wiki.gnuradio.org/index.php/BuildGuide) for all flags.
