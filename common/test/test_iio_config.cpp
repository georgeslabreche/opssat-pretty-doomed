/*
 * test_iio_config.cpp - Standalone IIO config write+readback test
 *
 * Tests write_iio_rx_config and readback_iio_rx_config against the SDR
 * emulator without GNU Radio (avoids QEMU SIGFPE in filter design).
 *
 * Build inside Docker:
 *   g++ -Wall -O3 -std=c++17 -I../../common/include test_iio_config.cpp -o test_iio_config -liio
 *
 * Usage:
 *   ./test_iio_config <uri> [frequency] [sdr_rate] [rf_bandwidth] [gain]
 *   ./test_iio_config ip:sdr-emu:30431
 *   ./test_iio_config ip:sdr-emu:30431 1296000000 2400000 200000 50
 */

#include <cstdlib>
#include <iostream>

#include "pretty_log.h"
#include "pretty_iio.h"

using namespace pretty;

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <uri> [frequency] [sdr_rate] [rf_bandwidth] [gain]\n";
        return 1;
    }

    const char* uri = argv[1];
    long long frequency   = (argc > 2) ? std::stoll(argv[2]) : 1296000000LL;
    long long sdr_rate    = (argc > 3) ? std::stoll(argv[3]) : 2400000LL;
    long long rf_bandwidth = (argc > 4) ? std::stoll(argv[4]) : 200000LL;
    double gain           = (argc > 5) ? std::stod(argv[5]) : 50.0;

    log_info() << "IIO Config Write+Readback Test\n";
    log_info() << "URI:          " << uri << "\n";
    log_info() << "Frequency:    " << frequency << " Hz\n";
    log_info() << "SDR rate:     " << sdr_rate << " Hz\n";
    log_info() << "RF bandwidth: " << rf_bandwidth << " Hz\n";
    log_info() << "Gain:         " << gain << " dB\n";

    // Connect
    log_info() << "Connecting to IIO context...\n";
    struct iio_context* ctx = iio_create_context_from_uri(uri);
    if (!ctx) {
        log_error() << "Could not connect to " << uri << "\n";
        return 1;
    }

    struct iio_device* phy = iio_context_find_device(ctx, "ad9361-phy");
    if (!phy) {
        log_error() << "No ad9361-phy device\n";
        iio_context_destroy(ctx);
        return 1;
    }

    // Readback BEFORE write
    log_info() << "--- BEFORE write ---\n";
    readback_iio_rx_config(phy, frequency, sdr_rate, rf_bandwidth, gain, 10, false);

    // Write
    log_info() << "--- WRITING config ---\n";
    if (!write_iio_rx_config(phy, frequency, sdr_rate, rf_bandwidth, gain)) {
        log_error() << "Write failed\n";
        iio_context_destroy(ctx);
        return 1;
    }

    // Readback AFTER write (strict mode)
    log_info() << "--- AFTER write (strict) ---\n";
    bool ok = readback_iio_rx_config(phy, frequency, sdr_rate, rf_bandwidth, gain, 10, true);

    iio_context_destroy(ctx);

    if (ok) {
        log_info() << "PASS: all readback values match\n";
        return 0;
    } else {
        log_error() << "FAIL: readback mismatch after write\n";
        return 1;
    }
}
