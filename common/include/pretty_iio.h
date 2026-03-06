/*
 * pretty_iio.h - AD9361 IIO configuration utilities for OPS-SAT PRETTY
 *
 * Header-only. Direct libiio calls for writing and reading back AD9361
 * attributes. The device_source/device_sink iio_param_vec_t mechanism
 * does not reliably apply attributes on the flatsat — these functions
 * match the OPS-SAT SDR experimenter code template approach.
 */
#ifndef PRETTY_IIO_H
#define PRETTY_IIO_H

#include <cmath>
#include <string>
#include <iio.h>

#include "pretty_log.h"

namespace pretty {

// Safe IIO channel attribute read — returns empty string on failure.
// iio_channel_attr_read() does not guarantee NUL-termination, so we
// construct std::string from (buf, n) to avoid reading past the payload.
inline std::string iio_attr_read_str(struct iio_channel* ch, const char* attr) {
    char buf[256];
    ssize_t n = iio_channel_attr_read(ch, attr, buf, sizeof(buf));
    if (n <= 0) return "";
    if ((size_t)n >= sizeof(buf)) {
        log_warning() << "IIO attr '" << attr << "' truncated (" << n << " bytes, buf=" << sizeof(buf) << ")\n";
        buf[sizeof(buf) - 1] = '\0';
        return std::string(buf, sizeof(buf) - 1);
    }
    // Strip trailing NUL bytes — iio_channel_attr_read() may include the
    // NUL terminator in the returned byte count, which would create a
    // std::string containing embedded NUL that breaks string comparisons.
    while (n > 0 && buf[n - 1] == '\0') n--;
    return std::string(buf, (size_t)n);
}

// Write AD9361 RX configuration via direct libiio calls.
inline bool write_iio_rx_config(struct iio_device* phy,
                                long long frequency, long long sdr_rate,
                                long long rf_bandwidth, double gain) {
    struct iio_channel* rx_lo = iio_device_find_channel(phy, "altvoltage0", true);
    struct iio_channel* rx0 = iio_device_find_channel(phy, "voltage0", false);
    if (!rx_lo || !rx0) {
        log_error() << "FATAL: could not find RX LO or voltage0 channel for config write\n";
        return false;
    }

    int ret;
    ret = iio_channel_attr_write_longlong(rx_lo, "frequency", frequency);
    if (ret < 0) {
        log_error() << "FATAL: could not write RX LO frequency (ret=" << ret << ")\n";
        return false;
    }

    ret = iio_channel_attr_write_longlong(rx0, "sampling_frequency", sdr_rate);
    if (ret < 0) {
        log_error() << "FATAL: could not write sampling_frequency (ret=" << ret << ")\n";
        return false;
    }

    ret = iio_channel_attr_write_longlong(rx0, "rf_bandwidth", rf_bandwidth);
    if (ret < 0) {
        log_error() << "FATAL: could not write rf_bandwidth (ret=" << ret << ")\n";
        return false;
    }

    ret = iio_channel_attr_write(rx0, "gain_control_mode", "manual");
    if (ret < 0) {
        log_error() << "FATAL: could not write gain_control_mode (ret=" << ret << ")\n";
        return false;
    }

    ret = iio_channel_attr_write_double(rx0, "hardwaregain", gain);
    if (ret < 0) {
        log_error() << "FATAL: could not write hardwaregain (ret=" << ret << ")\n";
        return false;
    }

    log_info() << "AD9361 RX config written via libiio\n";
    return true;
}

// Write AD9361 TX configuration via direct libiio calls.
// tx_gain is negative attenuation (e.g. -10.0 for 10 dB attenuation).
inline bool write_iio_tx_config(struct iio_device* phy,
                                long long frequency, long long sdr_rate,
                                long long rf_bandwidth, double tx_gain) {
    struct iio_channel* tx_lo = iio_device_find_channel(phy, "altvoltage1", true);
    struct iio_channel* tx0 = iio_device_find_channel(phy, "voltage0", true);
    if (!tx_lo || !tx0) {
        log_error() << "FATAL: could not find TX LO or TX voltage0 channel for config write\n";
        return false;
    }

    int ret;
    ret = iio_channel_attr_write_longlong(tx_lo, "frequency", frequency);
    if (ret < 0) {
        log_error() << "FATAL: could not write TX LO frequency (ret=" << ret << ")\n";
        return false;
    }

    ret = iio_channel_attr_write_longlong(tx0, "sampling_frequency", sdr_rate);
    if (ret < 0) {
        log_error() << "FATAL: could not write TX sampling_frequency (ret=" << ret << ")\n";
        return false;
    }

    ret = iio_channel_attr_write_longlong(tx0, "rf_bandwidth", rf_bandwidth);
    if (ret < 0) {
        log_error() << "FATAL: could not write TX rf_bandwidth (ret=" << ret << ")\n";
        return false;
    }

    ret = iio_channel_attr_write_double(tx0, "hardwaregain", tx_gain);
    if (ret < 0) {
        log_error() << "FATAL: could not write TX hardwaregain (ret=" << ret << ")\n";
        return false;
    }

    log_info() << "AD9361 TX config written via libiio\n";
    return true;
}

// Read back AD9361 RX configuration and warn if values differ from requested.
// When strict=true, sample rate mismatch is fatal (returns false).
// When strict=false (--min-readback), sample rate mismatch is a warning.
inline bool readback_iio_rx_config(struct iio_device* phy,
                                   long long frequency, long long sdr_rate,
                                   long long rf_bandwidth, double gain,
                                   long rate_tolerance, bool strict) {
    // RX LO frequency — always warn-only because the AD9361 PLL quantizes to the
    // nearest achievable frequency (typically a few Hz off). This is normal and has
    // no practical impact on reception. Sample rate mismatches, on the other hand,
    // are fatal (when strict) because they affect file sizes and downlink budget.
    struct iio_channel* rx_lo = iio_device_find_channel(phy, "altvoltage0", true);
    if (rx_lo) {
        std::string val = iio_attr_read_str(rx_lo, "frequency");
        if (!val.empty()) {
            log_info() << "AD9361 RX LO readback: " << val << " Hz\n";
            try {
                long long actual_freq = std::stoll(trim(val));
                if (actual_freq != frequency) {
                    log_warning() << "RX LO mismatch — requested " << frequency
                                << " Hz, got " << actual_freq << " Hz\n";
                }
            } catch (const std::exception& e) {
                log_warning() << "could not parse RX LO frequency: " << val
                              << " (" << e.what() << ")\n";
            }
        }
    }

    // RX channel attributes (voltage0, input) — sample rate confirmation is mandatory
    // for downlink budget correctness when strict=true.
    struct iio_channel* rx0 = iio_device_find_channel(phy, "voltage0", false);
    if (!rx0) {
        log_error() << "FATAL: could not find RX channel voltage0 — "
                    << "cannot confirm sample rate for budget correctness\n";
        return false;
    }

    std::string val;

    val = iio_attr_read_str(rx0, "sampling_frequency");
    if (val.empty()) {
        if (strict) {
            log_error() << "FATAL: could not read sampling_frequency — "
                        << "cannot confirm sample rate for budget correctness\n";
            return false;
        } else {
            log_warning() << "could not read sampling_frequency (--min-readback, continuing)\n";
        }
    } else {
        log_info() << "AD9361 sample rate readback: " << val << " Hz\n";
        try {
            long long actual_rate = std::stoll(trim(val));
            // AD9361 may quantize the sample rate by a few Hz (e.g. 2399999
            // vs 2400000).  Allow +/-rate_tolerance Hz before treating it as a mismatch.
            long long rate_delta = std::abs(actual_rate - sdr_rate);
            if (rate_delta > rate_tolerance) {
                if (strict) {
                    log_error() << "FATAL: sample rate mismatch — requested " << sdr_rate
                                << " Hz, got " << actual_rate
                                << " Hz (invalidates downlink budget and file size expectations)\n";
                    return false;
                } else {
                    log_warning() << "sample rate mismatch — requested " << sdr_rate
                                << " Hz, got " << actual_rate
                                << " Hz (--min-readback, continuing)\n";
                }
            } else if (rate_delta > 0) {
                log_info() << "AD9361 sample rate readback: " << actual_rate
                           << " Hz (within +/-" << rate_tolerance << " Hz of requested " << sdr_rate << " Hz)\n";
            }
        } catch (const std::exception& e) {
            if (strict) {
                log_error() << "FATAL: could not parse sampling_frequency '" << val
                            << "' (" << e.what()
                            << ") — cannot confirm sample rate for budget correctness\n";
                return false;
            } else {
                log_warning() << "could not parse sampling_frequency '" << val
                            << "' (" << e.what() << ") (--min-readback, continuing)\n";
            }
        }
    }

    val = iio_attr_read_str(rx0, "rf_bandwidth");
    if (!val.empty()) {
        log_info() << "AD9361 RX RF bandwidth readback: " << val << " Hz\n";
        try {
            long long actual_bw = std::stoll(trim(val));
            if (actual_bw != rf_bandwidth) {
                log_warning() << "RX RF bandwidth mismatch — requested " << rf_bandwidth
                            << " Hz, got " << actual_bw << " Hz\n";
            }
        } catch (const std::exception& e) {
            log_warning() << "could not parse RX RF bandwidth: " << val
                          << " (" << e.what() << ")\n";
        }
    }

    val = iio_attr_read_str(rx0, "gain_control_mode");
    if (!val.empty()) {
        log_info() << "AD9361 RX gain control mode: " << val << "\n";
        std::string mode = trim(val);
        if (mode != "manual") {
            log_warning() << "gain control mode mismatch — requested manual, got " << mode << "\n";
        }
    }

    val = iio_attr_read_str(rx0, "hardwaregain");
    if (!val.empty()) {
        log_info() << "AD9361 RX hardware gain: " << val << "\n";
        try {
            double actual_gain = std::stod(trim(val));
            if (std::abs(actual_gain - gain) > 0.5) {
                log_warning() << "RX gain mismatch — requested " << gain
                            << " dB, got " << actual_gain << " dB\n";
            }
        } catch (const std::exception& e) {
            log_warning() << "could not parse hardwaregain: " << val
                          << " (" << e.what() << ")\n";
        }
    }

    val = iio_attr_read_str(rx0, "rssi");
    if (!val.empty()) {
        log_info() << "AD9361 RX RSSI: " << val << "\n";
    }

    return true;
}

// Read back AD9361 TX configuration and warn if values differ from requested.
// tx_attenuation is the positive attenuation value (e.g. 10.0 for -10 dB gain).
inline bool readback_iio_tx_config(struct iio_device* phy,
                                   long long frequency,
                                   long long rf_bandwidth,
                                   double tx_attenuation) {
    struct iio_channel* tx_lo = iio_device_find_channel(phy, "altvoltage1", true);
    if (tx_lo) {
        std::string val = iio_attr_read_str(tx_lo, "frequency");
        if (!val.empty()) {
            log_info() << "AD9361 TX LO readback: " << val << " Hz\n";
            try {
                long long actual_freq = std::stoll(trim(val));
                if (actual_freq != frequency) {
                    log_warning() << "TX LO mismatch — requested " << frequency
                                << " Hz, got " << actual_freq << " Hz\n";
                }
            } catch (const std::exception& e) {
                log_warning() << "could not parse TX LO frequency: " << val
                              << " (" << e.what() << ")\n";
            }
        }
    }

    struct iio_channel* tx0 = iio_device_find_channel(phy, "voltage0", true);
    if (tx0) {
        std::string val;

        val = iio_attr_read_str(tx0, "rf_bandwidth");
        if (!val.empty()) {
            log_info() << "AD9361 TX RF bandwidth readback: " << val << " Hz\n";
            try {
                long long actual_bw = std::stoll(trim(val));
                if (actual_bw != rf_bandwidth) {
                    log_warning() << "TX RF bandwidth mismatch — requested " << rf_bandwidth
                                << " Hz, got " << actual_bw << " Hz\n";
                }
            } catch (const std::exception& e) {
                log_warning() << "could not parse TX RF bandwidth: " << val
                              << " (" << e.what() << ")\n";
            }
        }

        val = iio_attr_read_str(tx0, "hardwaregain");
        if (!val.empty()) {
            log_info() << "AD9361 TX hardware gain: " << val << "\n";
            try {
                double actual_atten = std::abs(std::stod(trim(val)));
                if (std::abs(actual_atten - tx_attenuation) > 0.5) {
                    log_warning() << "TX attenuation mismatch — requested " << tx_attenuation
                                << " dB, got " << actual_atten << " dB\n";
                }
            } catch (const std::exception& e) {
                log_warning() << "could not parse TX hardwaregain: " << val
                              << " (" << e.what() << ")\n";
            }
        }
    }

    return true;
}

} // namespace pretty

#endif // PRETTY_IIO_H
