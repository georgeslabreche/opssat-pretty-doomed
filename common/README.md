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

## Usage

Include from consuming apps with `-I/app/common/include` (Docker) or `-I../../common/include` (host).

All symbols are in the `pretty` namespace. Consuming apps use `using namespace pretty;` for backward compatibility with unqualified names.

```cpp
#include "pretty_log.h"
#include "pretty_signal.h"
#include "pretty_config.h"
#include "pretty_iio.h"
#include "pretty_audio.h"

using namespace pretty;
```

## Docker volume mount

Each app's `docker-compose.yml` mounts common into the container:

```yaml
volumes:
  - ../../../common:/app/common
```

## Tests

`test/test_iio_config.cpp` - Standalone IIO config write/readback test (no GNU Radio dependency). Connects directly to an IIO device via URI.

```
g++ -Wall -O3 -std=c++17 -I../../common/include test_iio_config.cpp -o test_iio_config -liio
./test_iio_config ip:sdr-emu:30431
```

## Dependencies

- `pretty_log.h`, `pretty_signal.h`, `pretty_config.h`: standard library only
- `pretty_iio.h`: libiio (`<iio.h>`)
- `pretty_audio.h`: libsndfile (`<sndfile.h>`)
