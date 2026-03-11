# common/ - Shared utilities for OPS-SAT PRETTY

Header-only C++17 library shared across PRETTY experiment apps (sdr-capture, sdr-loopback, pretty-doomed).

## Headers

| Header | Description |
|--------|-------------|
| `pretty_log.h` | Timestamped logging (`log_info`, `log_warning`, `log_error`) and `trim()` |
| `pretty_signal.h` | Signal handling (`g_running`, `g_signal_received`, `signal_handler`) |
| `pretty_config.h` | KEY=VALUE config file parser (`load_config_map`) |
| `pretty_iio.h` | AD9361 IIO config write/readback (`write_iio_rx_config`, `write_iio_tx_config`, `readback_iio_rx_config`, `readback_iio_tx_config`) |
| `pretty_audio.h` | Audio/IQ utilities (`rms_normalize`, `check_sc16_quality`, `make_iq_filename`, `IQ_SCALE`) |
| `pretty_spectrogram.h` | Spectrogram BMP generator for sc16 I/Q files (`generate_spectrogram`, `make_spectrogram_filename`) |

## Usage

Include from consuming apps with `-I/app/common/include` (Docker) or `-I../../../common/include` (host).

All symbols are in the `pretty` namespace. Consuming apps use `using namespace pretty;` for backward compatibility with unqualified names.

```cpp
#include "pretty_log.h"
#include "pretty_signal.h"
#include "pretty_config.h"
#include "pretty_iio.h"
#include "pretty_audio.h"
#include "pretty_spectrogram.h"

using namespace pretty;
```

## Docker volume mount

Each app's `docker-compose.yml` mounts common into the container:

```yaml
volumes:
  - ../../../common:/app/common
```

## Tests

### test_iio_config

Standalone IIO config write/readback test. Connects directly to an IIO device, writes RX configuration, and verifies the readback matches. No GNU Radio dependency — avoids the QEMU SIGFPE issues that occur with GNU Radio filter design on emulated ARM.

**Via Makefile target** (recommended — builds and runs in one step):

```bash
# Start the SDR emulator
docker-compose -f docker-compose.emu-test.yml up -d sdr-emu

# Build and run the test
docker-compose -f docker-compose.emu-test.yml run --rm sdr-capture make test-iio

# Clean up
docker-compose -f docker-compose.emu-test.yml down
```

**Manual build and run:**

```bash
# Build inside Docker
docker-compose -f docker-compose.emu-test.yml run --rm sdr-capture \
  g++ -Wall -O3 -std=c++17 -I/app/common/include \
  /app/common/test/test_iio_config.cpp -o build/test_iio_config -liio

# Run with defaults (1296 MHz, 2.4 MSPS, 200 kHz BW, 50 dB gain)
docker-compose -f docker-compose.emu-test.yml run --rm sdr-capture \
  ./build/test_iio_config ip:sdr-emu:30431

# Run with custom parameters
docker-compose -f docker-compose.emu-test.yml run --rm sdr-capture \
  ./build/test_iio_config ip:sdr-emu:30431 1176450000 3000000 300000 40
```

**What it tests:**
1. Reads back emulator defaults (expect mismatches — emulator starts with sample file parameters)
2. Writes the requested RX config via `write_iio_rx_config()`
3. Reads back in strict mode via `readback_iio_rx_config()` — all values must match

Exit code 0 = PASS, 1 = FAIL.

### test_log

Unit tests for `pretty_log.h`. No external dependencies — standard library only.

```bash
# Build and run inside Docker
docker-compose run --rm sdr-loopback sh -c \
  "g++ -Wall -O2 -std=c++17 -I/app/common/include -I/app/common/test \
   /app/common/test/test_log.cpp -o /app/build/test_log \
   && /app/build/test_log"
```

**What it tests:**
- `trim()` — whitespace stripping, trailing NUL byte handling (the v4 EM failure scenario), internal space preservation
- `local_tm()` — cross-platform local time conversion

Exit code 0 = all passed, 1 = at least one failure.

### test_config

Unit tests for `pretty_config.h`. No external dependencies — standard library only.

```bash
# Build and run inside Docker
docker-compose run --rm sdr-loopback sh -c \
  "g++ -Wall -O2 -std=c++17 -I/app/common/include -I/app/common/test \
   /app/common/test/test_config.cpp -o /app/build/test_config \
   && /app/build/test_config"
```

**What it tests:**
- `load_config_map()` — basic parsing, comments, blank lines, whitespace trimming, duplicate keys, missing files, values containing `=`

Exit code 0 = all passed, 1 = at least one failure.

### test_spectrogram

Standalone spectrogram generator test. Reads an sc16 I/Q file and produces a BMP spectrogram thumbnail. No GNU Radio or IIO dependency — only FFTW3.

```bash
# Build inside Docker (from sdr-capture)
docker-compose run --rm sdr-capture \
  g++ -Wall -O3 -std=c++17 -I/app/common/include \
  /app/common/test/test_spectrogram.cpp -o build/test_spectrogram -lfftw3f

# Run against an sc16 file (sample_rate in Hz)
docker-compose run --rm sdr-capture \
  ./build/test_spectrogram toGround/capture.sc16 2400000
```

Produces `spectrogram.bmp` in the same directory as the input file. Output is a 1024x256 BMP (~768 KB) with time on the x-axis and frequency (-fs/2 to +fs/2) on the y-axis.

## Dependencies

- `pretty_log.h`, `pretty_signal.h`, `pretty_config.h`: standard library only
- `pretty_iio.h`: libiio (`<iio.h>`)
- `pretty_audio.h`: libsndfile (`<sndfile.h>`)
- `pretty_spectrogram.h`: FFTW3 single-precision (`<fftw3.h>`, link with `-lfftw3f`)
