/**
 * capture.cpp - AD9361 RX capture for the pretty-doomed pipeline
 *
 * Adapted from sandbox/gnuradio/sdr-capture/src/capture_loop.cpp.
 * Captures RF audio, FM demodulates, and writes WAV + sc16 artifacts.
 */

#include "capture.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

#include <gnuradio/top_block.h>
#include <gnuradio/blocks/head.h>
#include <gnuradio/blocks/short_to_float.h>
#include <gnuradio/blocks/float_to_complex.h>
#include <gnuradio/blocks/complex_to_interleaved_short.h>
#include <gnuradio/blocks/file_sink.h>
#include <gnuradio/blocks/wavfile_sink.h>
#include <gnuradio/analog/quadrature_demod_cf.h>
#include <gnuradio/filter/rational_resampler.h>
#include <gnuradio/filter/fir_filter_blk.h>
#include <gnuradio/filter/firdes.h>
#include <gnuradio/iio/device_source.h>

#include <iio.h>
#include <ad9361.h>

#include "pretty_log.h"
#include "pretty_signal.h"
#include "pretty_iio.h"
#include "pretty_audio.h"
#include "pretty_spectrogram.h"
#include "pretty_constellation.h"

using namespace pretty;

bool run_capture(const PipelineConfig& cfg,
                     const std::string& output_dir,
                     CaptureResult& result) {
    result.success = false;

    // Derived rates
    if (cfg.sdr_decimation <= 0 || cfg.sdr_rate <= 0) {
        log_error() << "Invalid SDR rate or decimation\n";
        return false;
    }
    if (cfg.sdr_hw_fir_enable) {
        if (cfg.sdr_hw_fir_rate <= 0 || cfg.sdr_hw_fir_fpass <= 0 ||
            cfg.sdr_hw_fir_fstop <= 0 || cfg.sdr_hw_fir_wnom_tx <= 0 ||
            cfg.sdr_hw_fir_wnom_rx <= 0) {
            log_error() << "Hardware FIR enabled but parameters are missing or invalid\n";
            return false;
        }
    }

    // The rate entering GNU Radio: sdr_rate (no hw FIR) or sdr_hw_fir_rate (hw FIR)
    long gnuradio_input_rate = cfg.sdr_hw_fir_enable ? cfg.sdr_hw_fir_rate : cfg.sdr_rate;

    unsigned long effective_rate = gnuradio_input_rate / cfg.sdr_decimation;
    if (effective_rate == 0) {
        log_error() << "Effective rate is 0\n";
        return false;
    }

    // Output paths
    std::string wav_file = output_dir + "/capture.wav";
    std::string iq_file = output_dir + "/capture.sc16";

    // Sample counts
    long long iq_samples = (long long)cfg.sdr_duration * (long long)effective_rate;

    // Cap to downlink budget
    const long long max_iq_bytes = (long long)cfg.sdr_max_iq_mb * 1024LL * 1024;
    long long max_iq_samples = max_iq_bytes / 4;
    if (iq_samples > max_iq_samples) {
        log_warning() << "Capping capture to " << cfg.sdr_max_iq_mb << " MiB ("
                      << max_iq_samples << " samples)\n";
        iq_samples = max_iq_samples;
    }

    // Snap to resampler decimation multiple
    unsigned long rx_gcd = std::gcd((unsigned long)effective_rate, (unsigned long)cfg.sdr_audio_rate);
    unsigned long interpolation = cfg.sdr_audio_rate / rx_gcd;
    unsigned long decimation = effective_rate / rx_gcd;
    {
        long long rem = iq_samples % (long long)decimation;
        if (rem != 0) iq_samples -= rem;
    }
    if (iq_samples <= 0) {
        log_error() << "Capture duration too short\n";
        return false;
    }

    long long audio_samples = (iq_samples / (long long)decimation) * (long long)interpolation;
    double duration_sec = (double)iq_samples / effective_rate;

    log_info() << "Configuring SDR...\n";
    log_info() << "SDR Capture: " << duration_sec << "s at " << effective_rate << " Hz effective\n";
    log_info() << "  Frequency:   " << cfg.sdr_frequency / 1e6 << " MHz\n";
    if (cfg.sdr_hw_fir_enable) {
        log_info() << "  HW FIR:      " << cfg.sdr_rate << " Hz ADC -> "
                   << cfg.sdr_hw_fir_rate << " Hz post-FIR\n";
        log_info() << "  SDR rate:    " << gnuradio_input_rate << " Hz (post-FIR), decimation "
                   << cfg.sdr_decimation << "x\n";
    } else {
        log_info() << "  SDR rate:    " << cfg.sdr_rate << " Hz, decimation "
                   << cfg.sdr_decimation << "x\n";
    }
    log_info() << "  I/Q samples: " << iq_samples << ", audio samples: " << audio_samples << "\n";
    log_info() << "  Resample:    " << effective_rate << " -> " << cfg.sdr_audio_rate
               << " (interp=" << interpolation << ", decim=" << decimation << ")\n";

    // Design filters
    if (cfg.sdr_lpf_cutoff <= 0 || cfg.sdr_lpf_cutoff >= gnuradio_input_rate / 2.0) {
        log_error() << "LPF cutoff out of range\n";
        return false;
    }
    std::vector<float> lpf_taps = gr::filter::firdes::low_pass(
        1.0, gnuradio_input_rate, cfg.sdr_lpf_cutoff, cfg.sdr_lpf_transition,
        gr::fft::window::WIN_HAMMING
    );
    std::vector<float> bp_taps = gr::filter::firdes::band_pass(
        1.0, cfg.sdr_audio_rate, cfg.sdr_bandpass_low, cfg.sdr_bandpass_high, 200.0,
        gr::fft::window::WIN_HAMMING
    );
    log_info() << "  LPF taps: " << lpf_taps.size() << ", bandpass taps: " << bp_taps.size() << "\n";

    // Configure AD9361 via libiio.
    // This runs per-capture (not once per run) because run_capture() is designed
    // as a self-contained unit that can retry from scratch on failure. The AD9361
    // can enter a bad state after a failed capture (IIO driver cleanup latency),
    // so re-configuring from scratch each time is defensive. The overhead is
    // negligible (a few ms of IIO writes vs 20s+ capture wall time).
    {
        struct iio_context* cfg_ctx = iio_create_context_from_uri(cfg.sdr_uri.c_str());
        if (!cfg_ctx) {
            log_error() << "Could not connect to IIO at " << cfg.sdr_uri << "\n";
            return false;
        }
        struct iio_device* phy = iio_context_find_device(cfg_ctx, "ad9361-phy");
        if (!phy) {
            log_error() << "No ad9361-phy device\n";
            iio_context_destroy(cfg_ctx);
            return false;
        }

        if (cfg.sdr_hw_fir_enable) {
            // Hardware FIR path: write LO/gain manually, then library sets rate/bandwidth/FIR
            struct iio_channel* rx_lo = iio_device_find_channel(phy, "altvoltage0", true);
            struct iio_channel* rx0 = iio_device_find_channel(phy, "voltage0", false);
            if (!rx_lo || !rx0) {
                log_error() << "Could not find RX LO or voltage0 channel\n";
                iio_context_destroy(cfg_ctx);
                return false;
            }

            int ret;
            ret = iio_channel_attr_write_longlong(rx_lo, "frequency", cfg.sdr_frequency);
            if (ret < 0) {
                log_error() << "Could not write RX LO frequency (ret=" << ret << ")\n";
                iio_context_destroy(cfg_ctx);
                return false;
            }
            ret = iio_channel_attr_write(rx0, "gain_control_mode", "manual");
            if (ret < 0) {
                log_error() << "Could not write gain_control_mode (ret=" << ret << ")\n";
                iio_context_destroy(cfg_ctx);
                return false;
            }
            ret = iio_channel_attr_write_double(rx0, "hardwaregain", cfg.sdr_gain);
            if (ret < 0) {
                log_error() << "Could not write hardwaregain (ret=" << ret << ")\n";
                iio_context_destroy(cfg_ctx);
                return false;
            }

            log_info() << "AD9361 RX LO/gain written via libiio\n";
            log_info() << "Configuring AD9361 hardware FIR: rate=" << cfg.sdr_hw_fir_rate
                       << " Fpass=" << cfg.sdr_hw_fir_fpass
                       << " Fstop=" << cfg.sdr_hw_fir_fstop << "\n";

            ret = ad9361_set_bb_rate_custom_filter_manual(
                phy,
                (unsigned long)cfg.sdr_hw_fir_rate,
                (unsigned long)cfg.sdr_hw_fir_fpass,
                (unsigned long)cfg.sdr_hw_fir_fstop,
                (unsigned long)cfg.sdr_hw_fir_wnom_tx,
                (unsigned long)cfg.sdr_hw_fir_wnom_rx
            );
            if (ret < 0) {
                log_error() << "ad9361_set_bb_rate_custom_filter_manual failed (ret=" << ret << ")\n";
                iio_context_destroy(cfg_ctx);
                return false;
            }
            log_info() << "AD9361 hardware FIR configured\n";

            // Verify FIR is actually enabled
            int fir_enabled = 0;
            if (ad9361_get_trx_fir_enable(phy, &fir_enabled) == 0) {
                if (fir_enabled) {
                    log_info() << "AD9361 hardware FIR readback: enabled\n";
                } else {
                    log_error() << "AD9361 hardware FIR readback: disabled (expected enabled)\n";
                    iio_context_destroy(cfg_ctx);
                    return false;
                }
            } else {
                log_warning() << "Could not read back hardware FIR enable state\n";
            }

            // Read back FIR config (tap count and decimation factor)
            {
                char fir_buf[256] = {0};
                ssize_t nb = iio_device_attr_read(phy, "filter_fir_config", fir_buf, sizeof(fir_buf) - 1);
                if (nb > 0) {
                    log_info() << "AD9361 FIR config readback: " << fir_buf << "\n";
                } else {
                    log_warning() << "Could not read back filter_fir_config\n";
                }
            }

            if (cfg.sdr_min_readback) {
                log_info() << "Minimal readback: sample rate mismatch is non-fatal\n";
            }
            if (!readback_iio_rx_config(phy, cfg.sdr_frequency,
                                        (long long)cfg.sdr_hw_fir_rate,
                                        (long long)cfg.sdr_hw_fir_wnom_rx,
                                        cfg.sdr_gain,
                                        cfg.sdr_rate_tolerance, !cfg.sdr_min_readback)) {
                iio_context_destroy(cfg_ctx);
                return false;
            }
        } else {
            // Software-only decimation path
            // Disable FIR in case a previous run left it enabled
            int fir_ret = ad9361_set_trx_fir_enable(phy, 0);
            if (fir_ret < 0) {
                log_warning() << "Could not disable hardware FIR (ret=" << fir_ret
                              << "), may be normal if FIR was never enabled\n";
            }

            if (!write_iio_rx_config(phy, cfg.sdr_frequency, (long long)cfg.sdr_rate,
                                     (long long)cfg.sdr_rf_bandwidth, cfg.sdr_gain)) {
                iio_context_destroy(cfg_ctx);
                return false;
            }
            if (cfg.sdr_min_readback) {
                log_info() << "Minimal readback: sample rate mismatch is non-fatal\n";
            }
            if (!readback_iio_rx_config(phy, cfg.sdr_frequency, (long long)cfg.sdr_rate,
                                        (long long)cfg.sdr_rf_bandwidth, cfg.sdr_gain,
                                        cfg.sdr_rate_tolerance, !cfg.sdr_min_readback)) {
                iio_context_destroy(cfg_ctx);
                return false;
            }
        }

        iio_context_destroy(cfg_ctx);
    }

    // Build flowgraph with retry
    gr::top_block_sptr tb;
    gr::blocks::head::sptr iq_head, audio_head;

    const int MAX_BUILD_ATTEMPTS = 3;
    const int BUILD_RETRY_DELAY_SEC = 5;

    for (int attempt = 1; ; attempt++) {
    try {
        log_info() << "Building flowgraph...\n";
        tb = gr::make_top_block("sdr_capture");

        gr::iio::iio_param_vec_t no_params;
        std::vector<std::string> channels = {"voltage0", "voltage1"};
        auto iio_src = gr::iio::device_source::make(
            cfg.sdr_uri, "cf-ad9361-lpc", channels, "ad9361-phy",
            no_params, 0x8000);

        auto i_s2f   = gr::blocks::short_to_float::make(1, 2048.0f);
        auto q_s2f   = gr::blocks::short_to_float::make(1, 2048.0f);
        auto to_fc32 = gr::blocks::float_to_complex::make(1);

        auto lpf       = gr::filter::fir_filter_ccf::make(cfg.sdr_decimation, lpf_taps);
        auto to_short  = gr::blocks::complex_to_interleaved_short::make(false, IQ_SCALE);
        iq_head        = gr::blocks::head::make(sizeof(gr_complex), iq_samples);
        auto iq_sink   = gr::blocks::file_sink::make(sizeof(short), iq_file.c_str(), false);
        auto fm_demod  = gr::analog::quadrature_demod_cf::make(
            effective_rate / (2.0 * M_PI * cfg.sdr_fm_deviation)
        );
        auto resampler = gr::filter::rational_resampler_fff::make(interpolation, decimation);
        auto bandpass  = gr::filter::fir_filter_fff::make(1, bp_taps);
        audio_head     = gr::blocks::head::make(sizeof(float), audio_samples);
        auto wav_sink  = gr::blocks::wavfile_sink::make(
            wav_file.c_str(), 1, cfg.sdr_audio_rate,
            gr::blocks::FORMAT_WAV, gr::blocks::FORMAT_PCM_16
        );

        // IIO src -> fc32
        tb->connect(iio_src, 0, i_s2f, 0);
        tb->connect(iio_src, 1, q_s2f, 0);
        tb->connect(i_s2f, 0, to_fc32, 0);
        tb->connect(q_s2f, 0, to_fc32, 1);
        tb->connect(to_fc32, 0, lpf, 0);

        // Branch 1: sc16 file
        tb->connect(lpf, 0, iq_head, 0);
        tb->connect(iq_head, 0, to_short, 0);
        tb->connect(to_short, 0, iq_sink, 0);

        // Branch 2: FM demod -> audio WAV
        tb->connect(lpf, 0, fm_demod, 0);
        tb->connect(fm_demod, 0, resampler, 0);
        tb->connect(resampler, 0, bandpass, 0);
        tb->connect(bandpass, 0, audio_head, 0);
        tb->connect(audio_head, 0, wav_sink, 0);

        log_info() << "Starting capture...\n";
        tb->start();
        break;
    } catch (const std::exception& e) {
        tb.reset();
        iq_head.reset();
        audio_head.reset();

        if (attempt >= MAX_BUILD_ATTEMPTS) {
            log_error() << "Flowgraph failed after " << MAX_BUILD_ATTEMPTS
                        << " attempts: " << e.what() << "\n";
            return false;
        }
        log_warning() << "Flowgraph attempt " << attempt << "/" << MAX_BUILD_ATTEMPTS
                      << " failed: " << e.what() << ", retrying in "
                      << BUILD_RETRY_DELAY_SEC << "s\n";
        std::this_thread::sleep_for(std::chrono::seconds(BUILD_RETRY_DELAY_SEC));
    }
    }

    // Wait for capture with timeout
    auto start_time = std::chrono::steady_clock::now();
    int timeout_sec = cfg.sdr_duration * cfg.sdr_timeout_multiplier + 10;
    log_info() << "Timeout: " << timeout_sec << "s ("
               << cfg.sdr_timeout_multiplier << "x duration + 10)\n";

    bool timed_out = false;
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        auto elapsed = std::chrono::steady_clock::now() - start_time;
        auto elapsed_sec = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();

        if (iq_head->nitems_written(0) >= (uint64_t)iq_samples) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            log_info() << "Capture complete (I/Q: " << iq_head->nitems_written(0)
                       << "/" << iq_samples << ", audio: " << audio_head->nitems_written(0)
                       << "/" << audio_samples << ", " << elapsed_sec << "s)\n";
            break;
        }

        if (elapsed_sec > 0 && elapsed_sec % 5 == 0 &&
            std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() % 5000 < 100) {
            log_info() << "Progress [" << elapsed_sec << "s]: I/Q "
                       << iq_head->nitems_written(0) << "/" << iq_samples
                       << ", audio " << audio_head->nitems_written(0) << "/" << audio_samples << "\n";
        }

        if (elapsed >= std::chrono::seconds(timeout_sec)) {
            log_error() << "Timeout after " << elapsed_sec << "s\n";
            timed_out = true;
            tb->stop();
            break;
        }
    }

    bool interrupted = (g_signal_received != 0);
    if (interrupted) {
        log_info() << "Received signal " << g_signal_received << ", stopping...\n";
    }

    log_info() << "Stopping flowgraph...\n";
    tb->stop();

    // Wait for flowgraph with grace period
    {
        const int WAIT_GRACE_SEC = 10;
        std::atomic<bool> wait_done{false};
        std::thread wait_thread([&]() {
            tb->wait();
            wait_done = true;
        });
        auto wait_start = std::chrono::steady_clock::now();
        while (!wait_done) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (std::chrono::steady_clock::now() - wait_start >= std::chrono::seconds(WAIT_GRACE_SEC)) {
                log_error() << "tb->wait() hung for " << WAIT_GRACE_SEC
                            << "s, force-exiting\n";
                std::cerr.flush();
                std::cout.flush();
                wait_thread.detach();
                _Exit(2);
            }
        }
        wait_thread.join();
    }

    bool early_stop = timed_out || interrupted;

    // I/Q artifact generation (diagnostics, spectrogram, constellation)
    auto generate_artifacts = [
        iq_file, effective_rate,
        enable_spec = cfg.sdr_enable_spectrogram,
        enable_const = cfg.sdr_enable_constellation
    ]() {
        Sc16Stats iq_stats = analyze_sc16(iq_file);
        if (iq_stats.valid) {
            double rms_i_db = (iq_stats.rms_i > 0) ? 20.0 * std::log10(iq_stats.rms_i / IQ_SCALE) : -999.0;
            double rms_q_db = (iq_stats.rms_q > 0) ? 20.0 * std::log10(iq_stats.rms_q / IQ_SCALE) : -999.0;
            log_info() << "IQ diag:  " << iq_stats.samples << " samples analyzed\n";
            log_info() << "IQ RMS:   I=" << iq_stats.rms_i << " (" << rms_i_db << " dBFS)"
                       << "  Q=" << iq_stats.rms_q << " (" << rms_q_db << " dBFS)\n";
            double zero_pct_i = 100.0 * iq_stats.zero_i / iq_stats.samples;
            double zero_pct_q = 100.0 * iq_stats.zero_q / iq_stats.samples;
            log_info() << "IQ zeros: I=" << zero_pct_i << "%  Q=" << zero_pct_q << "%\n";
        }

        if (enable_spec) {
            std::string spec_file = make_spectrogram_filename(iq_file);
            log_info() << "Spectrogram: " << spec_file << "\n";
            if (!generate_spectrogram(iq_file, spec_file, (long long)effective_rate)) {
                log_warning() << "Spectrogram generation failed\n";
            }
        }
        if (enable_const) {
            std::string const_file = make_constellation_filename(iq_file);
            log_info() << "Constellation: " << const_file << "\n";
            if (!generate_constellation(iq_file, const_file)) {
                log_warning() << "Constellation generation failed\n";
            }
        }
    };

    // Normalize audio
    log_info() << "Normalizing audio...\n";

    if (cfg.process_mode == "background") {
        // Background mode: artifacts run async, can overlap with pipeline processing
        result.artifact_future = std::async(std::launch::async, generate_artifacts).share();
        if (!rms_normalize(wav_file, -20.0)) {
            log_warning() << "Audio normalization failed\n";
        }
    } else {
        // Sequential mode: normalize then artifacts inline
        if (!rms_normalize(wav_file, -20.0)) {
            log_warning() << "Audio normalization failed\n";
        }
        generate_artifacts();
    }

    result.wav_path = wav_file;
    result.sc16_path = iq_file;
    result.duration_sec = duration_sec;
    result.success = !early_stop;

    log_info() << "SDR capture " << (result.success ? "OK" : "partial") << "\n";
    return true;
}
