# GNU Radio Experiments for OPS-SAT PRETTY SEPP

This directory contains GNU Radio signal processing experiments designed for the OPS-SAT PRETTY Satellite Experimental Processing Platform (SEPP).

## Motivation

Capture amateur radio signals using the onboard SDR and process them with GNU Radio for signal analysis, demodulation, and experimentation.

## SDR Payload

The Software Defined Radio system utilizes the AD9361 transceiver IC from Analog Devices, which delivers dual wideband RF receivers with coverage spanning 70 MHz to 6 GHz and programmable modulation bandwidth up to 56 MHz.

### Operating Frequency Range

The onboard SDR can effectively capture signals from around 400MHz to 1.8GHz, despite the transceiver's broader theoretical capabilities.

### Amplification and Antennas

The system incorporates high-gain, low-noise amplifiers paired with two independent patch antennas, both tuned to 1.2 GHz frequency. This dual-antenna configuration supports specialized applications.

### Special Capabilities

The SDR enables passive reflectometry applications by allowing the reception of direct and reflected GNSS signals, making it valuable for remote sensing and Earth observation experiments.

### Integration

The SDR payload connects directly to the SEPP (Satellite Experimental Processing Platform), enabling researchers to leverage the platform's Linux-based environment and flexible processing capabilities for signal analysis and experimentation.

*Source: [OPS-SAT Payloads](https://opssat.esa.int/pretty/payloads/)*

---

## AD9361 Transceiver IC

The AD9361 is a high-performance, highly integrated RF Agile Transceiver designed by Analog Devices.

### Key Specifications

| Parameter | Value |
|-----------|-------|
| RX LO Range | 70 MHz to 6.0 GHz |
| TX LO Range | 47 MHz to 6.0 GHz |
| Channel Bandwidth | < 200 kHz to 56 MHz |
| ADC/DAC Resolution | 12-bit |
| Configuration | 2×2 MIMO (dual RX, dual TX) |
| Operation Modes | TDD and FDD |
| Package | 10 mm × 10 mm, 144-ball CSP_BGA |

### Features

- Independent automatic gain control (AGC) per channel
- DC offset correction
- Quadrature correction
- Digital filtering
- Comprehensive power-down modes

*Source: [AD9361 Datasheet (PDF)](https://www.analog.com/media/en/technical-documentation/data-sheets/ad9361.pdf)*

---

## Programming with libiio

[libiio](https://github.com/analogdevicesinc/libiio) is Analog Devices' cross-platform library for interfacing with Linux Industrial I/O (IIO) subsystem devices. It abstracts hardware details and provides a complete programming interface.

### Libraries Required

- **libiio**: Core IIO interface library
- **libad9361-iio**: AD9361-specific library for filter design and multi-chip sync

### IIO Device Structure

The AD9361 exposes multiple IIO devices:

| Device | Purpose |
|--------|---------|
| `ad9361-phy` | Physical layer control (LO, gain, filters) |
| `cf-ad9361-lpc` | RX streaming (capture IQ samples) |
| `cf-ad9361-dds-core-lpc` | TX streaming |

### Channel Organization

```
RX Channels:
  in_voltage0  - RX1 I/Q data
  in_voltage1  - RX2 I/Q data

TX Channels:
  out_voltage0 - TX1 I/Q data
  out_voltage1 - TX2 I/Q data

LO Control:
  out_altvoltage0 - RX LO frequency
  out_altvoltage1 - TX LO frequency
```

### Key Sysfs Attributes

Configuration via `/sys/bus/iio/devices/iio:deviceX/`:

| Attribute | Description |
|-----------|-------------|
| `in_voltage_sampling_frequency` | RX sample rate (521 kSPS to 61.44 MSPS) |
| `in_voltage_rf_bandwidth` | RX analog bandwidth |
| `out_altvoltage0_frequency` | RX LO frequency |
| `in_voltage0_gain_control_mode` | AGC mode (manual, fast_attack, slow_attack, hybrid) |
| `in_voltage0_hardwaregain` | Manual gain control |
| `filter_fir_config` | Digital FIR filter configuration |

### Example: Capturing IQ Samples with libiio

```c
#include <iio.h>
#include <stdio.h>
#include <stdint.h>

#define MHZ(x) ((long long)(x*1000000.0 + .5))
#define GHZ(x) ((long long)(x*1000000000.0 + .5))

int main(void)
{
    struct iio_context *ctx;
    struct iio_device *phy, *rx;
    struct iio_channel *rx0_i, *rx0_q, *chn;
    struct iio_buffer *rxbuf;

    // Create context (local or network)
    ctx = iio_create_context_from_uri("local:");
    // For remote: ctx = iio_create_context_from_uri("ip:192.168.2.1");

    // Get AD9361 PHY device for configuration
    phy = iio_context_find_device(ctx, "ad9361-phy");

    // Configure RX LO frequency (e.g., 145 MHz for 2m amateur band)
    chn = iio_device_find_channel(phy, "altvoltage0", true);
    iio_channel_attr_write_longlong(chn, "frequency", MHZ(145));

    // Configure RX sampling frequency
    chn = iio_device_find_channel(phy, "voltage0", false);
    iio_channel_attr_write_longlong(chn, "sampling_frequency", MHZ(2.5));
    iio_channel_attr_write_longlong(chn, "rf_bandwidth", MHZ(2));

    // Configure gain control
    iio_channel_attr_write(chn, "gain_control_mode", "slow_attack");

    // Get RX streaming device
    rx = iio_context_find_device(ctx, "cf-ad9361-lpc");

    // Enable I and Q channels
    rx0_i = iio_device_find_channel(rx, "voltage0", false);
    rx0_q = iio_device_find_channel(rx, "voltage1", false);
    iio_channel_enable(rx0_i);
    iio_channel_enable(rx0_q);

    // Create buffer (1M samples)
    rxbuf = iio_device_create_buffer(rx, 1024 * 1024, false);

    // Capture loop
    while (1) {
        void *p_dat, *p_end;
        ptrdiff_t p_inc;

        // Refill buffer with new samples
        iio_buffer_refill(rxbuf);

        p_inc = iio_buffer_step(rxbuf);
        p_end = iio_buffer_end(rxbuf);

        // Process IQ samples
        for (p_dat = iio_buffer_first(rxbuf, rx0_i); p_dat < p_end; p_dat += p_inc) {
            int16_t i = ((int16_t*)p_dat)[0];  // I component
            int16_t q = ((int16_t*)p_dat)[1];  // Q component

            // Process samples here (write to file, pipe to GNU Radio, etc.)
            printf("%d,%d\n", i, q);
        }
    }

    // Cleanup
    iio_buffer_destroy(rxbuf);
    iio_context_destroy(ctx);

    return 0;
}
```

### Command-Line Capture

Capture IQ samples directly using `iio_readdev`:

```bash
# Capture 1024 samples from AD9361 RX
iio_readdev -a -s 1024 cf-ad9361-lpc voltage0 voltage1 > samples.dat

# Set frequency first via iio_attr
iio_attr -c ad9361-phy altvoltage0 frequency 145000000
iio_attr -c ad9361-phy voltage0 sampling_frequency 2500000
iio_attr -c ad9361-phy voltage0 rf_bandwidth 2000000
```

### C++ Usage

There is no official C++ version of `ad9361-iiostream`. The official examples are all in C. However, there are two options for C++ development:

#### Option 1: Use C API with C++ compiler

The C example compiles with g++ since libiio has a C API. Use `extern "C"` if needed:

```cpp
extern "C" {
#include <iio.h>
}
```

#### Option 2: Use the iiopp C++ bindings

libiio provides C++ bindings in `bindings/cpp/`. These require C++17 (or Boost for older standards) and are disabled by default in CMake builds.

```cpp
#include <iio.hpp>
#include <iostream>
#include <iomanip>

using namespace iiopp;

std::string get(Attr const & att)
{
    char value[1024] = {0};
    att.read_raw(value, sizeof(value));
    return value;
}

int main()
{
    ContextPtr context = create_context(nullptr, nullptr);

    for (Device device : *context)
    {
        std::cout << "Device: " << device.id() << std::endl;

        if (auto name = device.name())
            std::cout << "  Name: " << std::quoted(std::string(*name)) << std::endl;

        for (auto att : device.attrs)
            std::cout << "  Attr " << att.name() << " = " << get(att) << std::endl;

        for (Channel channel : device)
        {
            std::cout << "  Channel: " << channel.id()
                      << " (output: " << channel.is_output() << ")" << std::endl;

            for (auto att : channel.attrs)
                std::cout << "    Attr " << att.name() << " = " << get(att) << std::endl;
        }
    }

    return 0;
}
```

### Integration with GNU Radio

The captured IQ samples can be processed with GNU Radio:

1. **File Source**: Write IQ to file, read with GNU Radio File Source block
2. **Named Pipe**: Create a FIFO and stream directly to GNU Radio
3. **gr-iio**: Use the [gr-iio](https://github.com/analogdevicesinc/gr-iio) blocks for direct AD9361 integration

```bash
# Create named pipe for streaming to GNU Radio
mkfifo /tmp/iq_pipe
./capture_iq > /tmp/iq_pipe &
# GNU Radio reads from /tmp/iq_pipe as File Source
```

---

## Amateur Radio Frequencies

Frequencies within the OPS-SAT SDR operating range (400 MHz - 1.8 GHz):

| Band | Frequency Range | Notes |
|------|-----------------|-------|
| 70 cm | 430-440 MHz | UHF amateur band |
| 23 cm | 1240-1300 MHz | Close to antenna tuning (1.2 GHz) |

The 23 cm band is particularly suitable given the patch antennas are tuned to 1.2 GHz.

---

## References

### Analog Devices Documentation

| Title | Link |
|-------|------|
| AD9361 Product Page | https://www.analog.com/en/products/ad9361.html |
| AD9361 Datasheet (PDF) | https://www.analog.com/media/en/technical-documentation/data-sheets/ad9361.pdf |
| AD9361 Linux Driver Wiki | https://wiki.analog.com/resources/tools-software/linux-drivers/iio-transceiver/ad9361 |

### libiio Resources

| Title | Link |
|-------|------|
| libiio GitHub Repository | https://github.com/analogdevicesinc/libiio |
| libad9361-iio GitHub Repository | https://github.com/analogdevicesinc/libad9361-iio |
| libiio Documentation | https://analogdevicesinc.github.io/documentation/software/libiio/index.html |
| ad9361-iiostream.c Example | https://github.com/analogdevicesinc/libiio/blob/main/examples/ad9361-iiostream.c |
| iiopp C++ Bindings | https://github.com/analogdevicesinc/libiio/tree/main/bindings/cpp |
| iiopp-enum.cpp Example | https://github.com/analogdevicesinc/libiio/blob/main/bindings/cpp/examples/iiopp-enum.cpp |
| iiopp.h API Reference | https://codedocs.xyz/analogdevicesinc/libiio/iiopp_8h.html |

### OPS-SAT

| Title | Link |
|-------|------|
| OPS-SAT Payloads | https://opssat.esa.int/pretty/payloads/ |
