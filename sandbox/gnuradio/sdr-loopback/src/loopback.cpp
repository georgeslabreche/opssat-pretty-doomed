/*
 * loopback_test.cpp - GNU Radio IIO Loopback Test for OPS-SAT PRETTY
 *
 * Validates AD9361 SDR integration using internal loopback mode.
 * TX: WAV file -> resample -> FM mod -> AD9361 TX (sdr_rate e.g. 2.4 MSPS)
 * RX: AD9361 RX (sdr_rate) -> Decimating LPF (decim=12 -> 200 kSPS effective)
 *     -> sc16 I/Q + FM demod -> bandpass -> 16 kHz WAV
 *
 * Loopback mode routes TX internally to RX (no RF emission).
 *
 * Split into headers: loopback_config.h, loopback_quality.h, loopback_wav.h,
 * loopback_iio.h, loopback_flowgraph.h. This file contains only main().
 *
 * Usage: loopback_test -i input.wav -o output.wav [-c config.cfg]
 */

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>
#include <csignal>
#include <cerrno>
#include <cstring>
#include <cmath>
#include <numeric>
#include <chrono>
#include <thread>
#include <sched.h>
#include <iio.h>

#include "pretty_log.h"
#include "pretty_signal.h"
#include "loopback_config.h"
#include "loopback_wav.h"
#include "loopback_iio.h"
#include "loopback_flowgraph.h"

using namespace pretty;

int main(int argc, char* argv[]) {
    LoopbackConfig cfg;

    try {
        int rc = parse_args(argc, argv, cfg);
        if (rc == 2) return 0;  // --help
        if (rc != 0) return rc;
    } catch (const std::exception& e) {
        log_error() << "Invalid argument value: " << e.what() << "\n";
        print_usage(argv[0]);
        return 1;
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Pin to CPU 0 if requested — serializes all GNU Radio scheduler threads
    // onto one core to diagnose multi-threading race conditions.
    if (cfg.single_core) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(0, &cpuset);
        if (sched_setaffinity(0, sizeof(cpuset), &cpuset) == 0) {
            log_info() << "Pinned to CPU 0 (--single-core)\n";
        } else {
            log_warning() << "sched_setaffinity failed: " << strerror(errno)
                          << " (continuing without CPU pinning)\n";
        }
    }

    log_config(cfg);

    // Validate tx_mode
    if (cfg.tx_mode != "default" && cfg.tx_mode != "staggered" && cfg.tx_mode != "cyclic") {
        log_error() << "Invalid tx_mode '" << cfg.tx_mode
                    << "' (must be default, staggered, or cyclic)\n";
        return 1;
    }

    // --- Enable Loopback Mode via libiio (skip in file loopback mode) ---
    std::string prev_loopback = "0";
    if (cfg.use_file_loopback) {
        log_info() << "File loopback mode — skipping IIO loopback setup\n";
    } else {
    // Context is created, used to enable loopback, then destroyed BEFORE
    // run_loopback() creates GNU Radio blocks (which open their own contexts).
    // Keeping this context alive during flowgraph execution causes segfaults
    // due to IIO device conflicts with the device_source/sink contexts.
    {
        log_info() << "Connecting to IIO context for loopback setup...\n";
        struct iio_context* ctx = iio_create_context_from_uri(cfg.uri.c_str());
        if (!ctx) {
            log_error() << "Could not connect to IIO context at " << cfg.uri << "\n";
            return 1;
        }

        struct iio_device* phy = iio_context_find_device(ctx, "ad9361-phy");
        if (!phy) {
            log_error() << "Could not find ad9361-phy device\n";
            iio_context_destroy(ctx);
            return 1;
        }

        // Save current loopback value for restore on exit
        {
            char lb_buf[64];
            ssize_t rb = iio_device_debug_attr_read(phy, "loopback", lb_buf, sizeof(lb_buf));
            if (rb <= 0) {
                log_warning() << "Could not read loopback debug attribute, will restore to '0'\n";
            } else if ((size_t)rb >= sizeof(lb_buf)) {
                log_warning() << "Loopback attr truncated (" << rb << " bytes, buf="
                              << sizeof(lb_buf) << "), will restore to '0'\n";
            } else {
                prev_loopback = trim(std::string(lb_buf, (size_t)rb));
                log_info() << "AD9361 loopback current value: " << prev_loopback << "\n";
            }
        }

        log_info() << "Enabling loopback mode (writing '1' to loopback debug attr)...\n";
        int ret = iio_device_debug_attr_write(phy, "loopback", "1");
        if (ret < 0) {
            log_error() << "FATAL: could not enable loopback (ret=" << ret
                        << "). Loopback validation requires working loopback mode.\n";
            iio_context_destroy(ctx);
            return 1;
        }
        log_info() << "Loopback write OK (wrote " << ret << " bytes)\n";

        // Read back loopback attribute to confirm actual mode
        {
            char lb_buf[64];
            ssize_t rb = iio_device_debug_attr_read(phy, "loopback", lb_buf, sizeof(lb_buf));
            if (rb <= 0) {
                log_error() << "FATAL: could not read back loopback debug attribute — "
                            << "cannot confirm loopback is active\n";
                iio_context_destroy(ctx);
                restore_loopback(cfg.uri, prev_loopback);
                return 1;
            } else if ((size_t)rb >= sizeof(lb_buf)) {
                log_error() << "FATAL: loopback readback truncated (" << rb << " bytes, buf="
                            << sizeof(lb_buf) << ") — cannot confirm loopback is active\n";
                iio_context_destroy(ctx);
                restore_loopback(cfg.uri, prev_loopback);
                return 1;
            } else {
                std::string readback = trim(std::string(lb_buf, (size_t)rb));
                log_info() << "AD9361 loopback readback: " << readback << "\n";
                if (readback != "1") {
                    log_error() << "FATAL: loopback readback is '" << readback
                                << "', expected '1' — loopback not confirmed active\n";
                    iio_context_destroy(ctx);
                    restore_loopback(cfg.uri, prev_loopback);
                    return 1;
                }
            }
        }

        // Destroy context — loopback setting persists in the kernel driver.
        // No IIO context will be alive during flowgraph execution.
        iio_context_destroy(ctx);
        log_info() << "Loopback setup context released\n";
    }
    } // end if (!use_file_loopback)

    // Read input WAV (samples loaded via libsndfile for deterministic float type)
    int input_audio_rate = 0;
    long long input_frames = 0;
    std::vector<float> input_samples;
    double duration_sec = read_input_wav(cfg.input_wav, input_audio_rate, input_frames, input_samples);
    if (duration_sec < 0) {
        if (!cfg.use_file_loopback) restore_loopback(cfg.uri, prev_loopback);
        return 1;
    }
    log_info() << "Input frames:     " << input_frames << " (" << duration_sec << " sec)\n";

    // Cap duration to stay within downlink budget (sc16: 4 bytes per complex sample)
    // File is written at effective_rate (post-LPF decimation)
    const long long max_iq_bytes = (long long)cfg.max_iq_mb * 1024LL * 1024;
    double max_duration = (double)max_iq_bytes / (cfg.effective_rate * 4.0);
    log_info() << "Capture cap:      " << max_duration << "s ("
               << cfg.max_iq_mb << " MiB @ "
               << cfg.effective_rate << " SPS sc16)\n";
    if (duration_sec > max_duration) {
        log_warning() << "Input duration " << duration_sec << "s exceeds cap, truncating\n";
        duration_sec = max_duration;
    }

    // Snap to sample boundary at effective_rate for deterministic file sizes
    long long snap_samples = (long long)(duration_sec * cfg.effective_rate);
    if (snap_samples <= 0) {
        log_error() << "Effective duration must be > 0 (increase max_iq_mb or input length)\n";
        if (!cfg.use_file_loopback) restore_loopback(cfg.uri, prev_loopback);
        return 1;
    }
    // Align to RX decimation multiple *before* computing TX need_in,
    // otherwise we can falsely reject "too short" WAVs.
    unsigned long rx_gcd_m = std::gcd((unsigned long)cfg.effective_rate, (unsigned long)cfg.audio_rate);
    unsigned long rx_decim_m = cfg.effective_rate / rx_gcd_m;
    {
        long long rem = snap_samples % (long long)rx_decim_m;
        if (rem != 0) {
            long long snapped = snap_samples - rem;
            if (snapped <= 0) {
                log_error() << "Effective duration too short after RX alignment snap\n";
                if (!cfg.use_file_loopback) restore_loopback(cfg.uri, prev_loopback);
                return 1;
            }
            log_info() << "Aligning snap_samples to RX decim: " << snap_samples
                       << " -> " << snapped << " (rx_decim=" << rx_decim_m << ")\n";
            snap_samples = snapped;
        }
    }

    log_info() << "Effective duration: " << (double)snap_samples / cfg.effective_rate << " sec ("
               << snap_samples << " samples @ " << cfg.effective_rate << " Hz)\n";

    // TX input is not truncated — tx_head in the flowgraph caps output to tx_samples.
    // Truncating here would risk starving the resampler due to filter group delay.
    // The full (already duration-capped) input is passed; any excess is simply unused.

    // Staggered mode: prepend silence to TX audio. TX DMA starts simultaneously
    // with RX but sends zeros first. Tests whether Q dropout is caused by TX
    // content (modulated signal) vs TX DMA activity alone.
    if (cfg.tx_mode == "staggered" && cfg.enable_tx && cfg.tx_startup_delay > 0) {
        long long silence_samples = (long long)input_audio_rate * cfg.tx_startup_delay;
        log_info() << "Staggered TX: prepending " << silence_samples
                   << " silence samples (" << cfg.tx_startup_delay << "s)\n";
        std::vector<float> padded(silence_samples + input_samples.size(), 0.0f);
        std::copy(input_samples.begin(), input_samples.end(),
                  padded.begin() + silence_samples);
        input_samples = std::move(padded);
    }

    int rc;
    try {
        if (cfg.use_file_loopback) {
            rc = run_file_loopback(cfg, input_audio_rate, snap_samples, input_samples);
        } else {
            rc = run_loopback(cfg, input_audio_rate, snap_samples, input_samples, prev_loopback);
        }
    } catch (const std::exception& e) {
        log_error() << e.what() << "\n";
        rc = 1;
    }

    // Restore loopback via a fresh context (no conflicts with GNU Radio)
    if (!cfg.use_file_loopback) {
        restore_loopback(cfg.uri, prev_loopback);
    }
    return rc;
}
