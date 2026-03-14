#ifndef LOOPBACK_FLOWGRAPH_H
#define LOOPBACK_FLOWGRAPH_H

#include <fstream>
#include <chrono>
#include <thread>
#include <cmath>
#include <numeric>
#include <vector>
#include <string>
#include <atomic>

#include <gnuradio/top_block.h>
#include <gnuradio/blocks/vector_source.h>
#include <gnuradio/blocks/wavfile_sink.h>
#include <gnuradio/blocks/file_sink.h>
#include <gnuradio/blocks/file_source.h>
#include <gnuradio/blocks/multiply_const.h>
#include <gnuradio/blocks/head.h>
#include <gnuradio/blocks/short_to_float.h>
#include <gnuradio/blocks/float_to_complex.h>
#include <gnuradio/blocks/complex_to_float.h>
#include <gnuradio/blocks/float_to_short.h>
#include <gnuradio/blocks/complex_to_interleaved_short.h>
#include <gnuradio/analog/frequency_modulator_fc.h>
#include <gnuradio/analog/quadrature_demod_cf.h>
#include <gnuradio/filter/rational_resampler.h>
#include <gnuradio/filter/fir_filter_blk.h>
#include <gnuradio/filter/firdes.h>
#include <gnuradio/iio/device_source.h>
#include <gnuradio/iio/device_sink.h>

#include <iio.h>
#include <sndfile.h>

#include "pretty_log.h"
#include "pretty_signal.h"
#include "pretty_iio.h"
#include "pretty_audio.h"
#include "pretty_spectrogram.h"
#include "pretty_constellation.h"
#include "loopback_config.h"
#include "loopback_quality.h"
#include "loopback_iio.h"

using namespace pretty;

// File-based loopback: TX flowgraph writes gr_complex to file, RX flowgraph reads it back.
// No IIO hardware needed — tests the full FM mod/demod DSP chain locally.
// Returns 0 on success, 1 on error.
inline int run_file_loopback(const LoopbackConfig& cfg,
                      int input_audio_rate, long long iq_sample_count,
                      const std::vector<float>& input_samples) {
    g_running = 1;
    g_signal_received = 0;

    if (cfg.effective_rate == 0 || cfg.audio_rate <= 0 || input_audio_rate <= 0) {
        log_error() << "Invalid rates for file loopback\n";
        return 1;
    }

    std::string iq_file = make_iq_filename(cfg.output_wav);

    // TX resampling: input audio rate -> sdr_rate
    unsigned long tx_gcd = std::gcd((unsigned long)cfg.sdr_rate, (unsigned long)input_audio_rate);
    unsigned long tx_interp = cfg.sdr_rate / tx_gcd;
    unsigned long tx_decim = input_audio_rate / tx_gcd;
    log_info() << "TX resample:      " << input_audio_rate << " -> " << cfg.sdr_rate
               << " (interp=" << tx_interp << ", decim=" << tx_decim << ")\n";

    // RX resampling: effective_rate -> audio_rate
    unsigned long rx_gcd = std::gcd((unsigned long)cfg.effective_rate, (unsigned long)cfg.audio_rate);
    unsigned long rx_interp = cfg.audio_rate / rx_gcd;
    unsigned long rx_decim = cfg.effective_rate / rx_gcd;
    log_info() << "RX resample:      " << cfg.effective_rate << " -> " << cfg.audio_rate
               << " (interp=" << rx_interp << ", decim=" << rx_decim << ")\n";

    // Sample counts (same math as IIO mode)
    long long iq_samples = iq_sample_count;
    long long tx_samples = iq_samples * (long long)cfg.decimation;
    if (iq_samples % (long long)rx_decim != 0) {
        iq_samples = (iq_samples / (long long)rx_decim) * (long long)rx_decim;
        tx_samples = iq_samples * (long long)cfg.decimation;
        if (iq_samples <= 0) {
            log_error() << "iq_samples snapped to 0 — duration too short\n";
            return 1;
        }
    }
    long long rx_audio_samples = (iq_samples / (long long)rx_decim) * (long long)rx_interp;

    log_info() << "TX samples:       " << tx_samples << " (at " << cfg.sdr_rate << " Hz)\n";
    log_info() << "RX I/Q samples:   " << iq_samples << " (at " << cfg.effective_rate << " Hz post-LPF)\n";
    log_info() << "RX audio samples: " << rx_audio_samples << " (at " << cfg.audio_rate << " Hz)\n";

    // Filter design
    if (cfg.lpf_cutoff <= 0 || cfg.lpf_cutoff >= cfg.sdr_rate / 2.0) {
        log_error() << "LPF cutoff out of range\n";
        return 1;
    }
    std::vector<float> lpf_taps = gr::filter::firdes::low_pass(
        1.0, cfg.sdr_rate, cfg.lpf_cutoff, cfg.lpf_transition,
        gr::fft::window::WIN_HAMMING
    );
    std::vector<float> bp_taps = gr::filter::firdes::band_pass(
        1.0, cfg.audio_rate, cfg.bandpass_low, cfg.bandpass_high, 200.0,
        gr::fft::window::WIN_HAMMING
    );
    log_info() << "LPF taps: " << lpf_taps.size() << ", Bandpass taps: " << bp_taps.size() << "\n";

    // === TX flowgraph: WAV -> [resample] -> [FM mod] -> file ===
    log_info() << "Running TX flowgraph (writing to " << cfg.loopback_file << ")...\n";
    {
        auto tb = gr::make_top_block("tx_to_file");
        auto wav_src      = gr::blocks::vector_source_f::make(input_samples, false);
        auto tx_head      = gr::blocks::head::make(sizeof(float), tx_samples);
        auto scaler       = gr::blocks::multiply_const_ff::make(0.8);
        auto file_snk     = gr::blocks::file_sink::make(sizeof(gr_complex),
                                                         cfg.loopback_file.c_str(), false);

        // wav -> [resampler] -> head -> scaler
        gr::basic_block_sptr tx_prev = wav_src;
        if (cfg.enable_resampler_tx) {
            auto resampler_tx = gr::filter::rational_resampler_fff::make(tx_interp, tx_decim);
            tb->connect(tx_prev, 0, resampler_tx, 0);
            tx_prev = resampler_tx;
        }
        tb->connect(tx_prev, 0, tx_head, 0);
        tb->connect(tx_head, 0, scaler, 0);

        // scaler -> [FM mod] -> file
        if (cfg.enable_fm_mod) {
            double sensitivity = 2.0 * M_PI * cfg.fm_deviation / cfg.sdr_rate;
            auto fm_mod = gr::analog::frequency_modulator_fc::make(sensitivity);
            tb->connect(scaler, 0, fm_mod, 0);
            tb->connect(fm_mod, 0, file_snk, 0);
        } else {
            // No FM mod: wrap float into complex (Q=0) so file format stays gr_complex
            auto f2c = gr::blocks::float_to_complex::make(1);
            tb->connect(scaler, 0, f2c, 0);
            tb->connect(f2c, 0, file_snk, 0);
        }

        tb->run();
    }
    log_info() << "TX flowgraph complete\n";

    // Verify loopback file was written
    {
        std::ifstream f(cfg.loopback_file, std::ios::binary | std::ios::ate);
        if (!f.is_open()) {
            log_error() << "TX flowgraph produced no output file: " << cfg.loopback_file << "\n";
            return 1;
        }
        long long file_bytes = (long long)f.tellg();
        log_info() << "Loopback file: " << file_bytes << " bytes ("
                   << file_bytes / (long long)sizeof(gr_complex) << " complex samples, expected "
                   << tx_samples << ")\n";
        if (file_bytes == 0) {
            log_error() << "Loopback file is empty\n";
            return 1;
        }
    }

    // === RX flowgraph: file -> [LPF] -> sc16 + [FM demod] -> [resample] -> [bandpass] -> WAV ===
    log_info() << "Running RX flowgraph (reading from " << cfg.loopback_file << ")...\n";
    {
        auto tb = gr::make_top_block("rx_from_file");
        auto file_src     = gr::blocks::file_source::make(sizeof(gr_complex),
                                                           cfg.loopback_file.c_str(), false);
        auto to_short     = gr::blocks::complex_to_interleaved_short::make(false, IQ_SCALE);
        auto iq_head      = gr::blocks::head::make(sizeof(gr_complex), iq_samples);
        auto iq_sink_blk  = gr::blocks::file_sink::make(sizeof(short), iq_file.c_str(), false);
        auto rx_head      = gr::blocks::head::make(sizeof(float), rx_audio_samples);
        auto wav_sink     = gr::blocks::wavfile_sink::make(
            cfg.output_wav.c_str(), 1, cfg.audio_rate,
            gr::blocks::FORMAT_WAV, gr::blocks::FORMAT_PCM_16
        );

        // file -> [LPF] -> complex output
        gr::basic_block_sptr rx_complex = file_src;
        if (cfg.enable_lpf) {
            auto lpf = gr::filter::fir_filter_ccf::make(cfg.decimation, lpf_taps);
            tb->connect(rx_complex, 0, lpf, 0);
            rx_complex = lpf;
        }

        // Branch 1: sc16 file (always active)
        tb->connect(rx_complex, 0, iq_head, 0);
        tb->connect(iq_head, 0, to_short, 0);
        tb->connect(to_short, 0, iq_sink_blk, 0);

        // Branch 2: audio path
        gr::basic_block_sptr rx_audio;
        if (cfg.enable_fm_demod) {
            auto fm_demod = gr::analog::quadrature_demod_cf::make(
                cfg.effective_rate / (2.0 * M_PI * cfg.fm_deviation)
            );
            tb->connect(rx_complex, 0, fm_demod, 0);
            rx_audio = fm_demod;
        } else {
            // Extract real part of complex as float
            auto c2f = gr::blocks::complex_to_float::make(1);
            tb->connect(rx_complex, 0, c2f, 0);
            rx_audio = c2f;
        }
        if (cfg.enable_resampler_rx) {
            auto resampler_rx = gr::filter::rational_resampler_fff::make(rx_interp, rx_decim);
            tb->connect(rx_audio, 0, resampler_rx, 0);
            rx_audio = resampler_rx;
        }
        if (cfg.enable_bandpass) {
            auto bandpass = gr::filter::fir_filter_fff::make(1, bp_taps);
            tb->connect(rx_audio, 0, bandpass, 0);
            rx_audio = bandpass;
        }
        tb->connect(rx_audio, 0, rx_head, 0);
        tb->connect(rx_head, 0, wav_sink, 0);

        tb->run();
    }
    log_info() << "RX flowgraph complete\n";

    // === Post-processing (same as IIO mode) ===
    bool hard_fail = false;

    // RMS normalization
    log_info() << "Normalizing audio...\n";
    if (!rms_normalize(cfg.output_wav, -20.0)) {
        log_warning() << "audio normalization failed\n";
    }

    // Verify sc16 file
    {
        std::ifstream iq_check(iq_file, std::ios::binary | std::ios::ate);
        if (iq_check.is_open()) {
            long long actual_bytes = (long long)iq_check.tellg();
            if (actual_bytes == 0) {
                log_error() << "FAIL: sc16 file is empty\n";
                hard_fail = true;
            } else {
                long long actual_samples = actual_bytes / 4;
                long long expected_bytes = iq_samples * 4LL;
                log_info() << "sc16 file: " << actual_bytes << " bytes, "
                           << actual_samples << " samples (expected " << iq_samples << ")\n";
                if (actual_bytes != expected_bytes) {
                    double deviation = std::abs((double)(actual_bytes - expected_bytes) / expected_bytes);
                    if (deviation > 0.05) {
                        log_error() << "FAIL: sc16 size deviation " << (deviation * 100.0) << "%\n";
                        hard_fail = true;
                    }
                }
            }
        } else {
            log_error() << "FAIL: Could not open sc16 file: " << iq_file << "\n";
            hard_fail = true;
        }
    }

    // I/Q quality check
    double clip_rate;
    int peak_abs;
    check_sc16_quality(iq_file, (long long)cfg.effective_rate, clip_rate, peak_abs);
    if (clip_rate >= 0.0) {
        double peak_dbfs = (peak_abs > 0) ? 20.0 * std::log10((double)peak_abs / IQ_SCALE) : -999.0;
        log_info() << "sc16 peak: " << peak_abs << "/" << (int)IQ_SCALE
                   << " (" << peak_dbfs << " dBFS@IQ_SCALE)\n";
    }

    // Spectrogram
    if (cfg.enable_spectrogram) {
        std::string spec_file = make_spectrogram_filename(iq_file);
        if (!generate_spectrogram(iq_file, spec_file, (long long)cfg.effective_rate)) {
            log_warning() << "spectrogram generation failed\n";
        }
    }

    // Constellation
    if (cfg.enable_constellation) {
        std::string const_file = make_constellation_filename(iq_file);
        if (!generate_constellation(iq_file, const_file)) {
            log_warning() << "constellation generation failed\n";
        }
    }

    // Loopback quality validation
    double lag_ms = 0.0;
    double correlation = loopback_quality_check(cfg.input_wav, cfg.output_wav, lag_ms);
    if (correlation < 0.0) {
        log_warning() << "could not compute loopback quality metric\n";
    } else {
        log_info() << "Loopback quality: correlation=" << correlation
                   << ", lag=" << lag_ms << " ms\n";
        if (correlation < 0.3) {
            log_error() << "FAIL: loopback correlation " << correlation
                        << " < 0.3 — DSP chain output does not resemble input\n";
            hard_fail = true;
        } else if (correlation < 0.7) {
            log_warning() << "loopback correlation " << correlation << " < 0.7 — marginal\n";
        } else {
            log_info() << "Loopback PASS: correlation " << correlation << " >= 0.7\n";
        }
    }

    // Clean up loopback file
    std::remove(cfg.loopback_file.c_str());

    return hard_fail ? 1 : 0;
}

// Build GNU Radio flowgraph, run loopback test, then post-process audio.
// iq_sample_count is the pre-snapped integer sample count (avoids float->int truncation).
// input_samples: PCM float samples read via libsndfile (deterministic type).
// prev_loopback: value to restore on forced exit (_Exit path bypasses RAII).
inline int run_loopback(const LoopbackConfig& cfg,
                 int input_audio_rate, long long iq_sample_count,
                 const std::vector<float>& input_samples,
                 const std::string& prev_loopback) {
    // Reset signal flags (safe for multi-run or future use)
    g_running = 1;
    g_signal_received = 0;

    if (cfg.effective_rate == 0 || cfg.audio_rate <= 0) {
        log_error() << "Invalid rates: effective_rate and audio_rate must be > 0\n";
        return 1;
    }
    if (input_audio_rate <= 0) {
        log_error() << "Invalid input audio rate: " << input_audio_rate << "\n";
        return 1;
    }

    std::string iq_file = make_iq_filename(cfg.output_wav);

    log_info() << "Input audio rate: " << input_audio_rate << " Hz\n";
    log_info() << "Output audio rate: " << cfg.audio_rate << " Hz\n";

    // TX resampling: input audio rate -> SDR hardware rate (sdr_rate)
    unsigned long tx_gcd = std::gcd((unsigned long)cfg.sdr_rate, (unsigned long)input_audio_rate);
    unsigned long tx_interp = cfg.sdr_rate / tx_gcd;
    unsigned long tx_decim = input_audio_rate / tx_gcd;
    log_info() << "TX resample:      " << input_audio_rate << " -> " << cfg.sdr_rate
               << " (interp=" << tx_interp << ", decim=" << tx_decim << ")\n";
    // Sanity cap: absurd ratios produce huge FIR filter tap counts and blow memory
    if (tx_interp > 1000 || tx_decim > 1000) {
        log_error() << "TX resampler ratio " << tx_interp << "/" << tx_decim
                    << " too large (sdr_rate=" << cfg.sdr_rate
                    << ", input_audio_rate=" << input_audio_rate
                    << ") — choose rates with a reasonable GCD\n";
        return 1;
    }

    // RX resampling: effective_rate (post-LPF) -> output audio rate (always 16 kHz)
    unsigned long rx_gcd = std::gcd((unsigned long)cfg.effective_rate, (unsigned long)cfg.audio_rate);
    unsigned long rx_interp = cfg.audio_rate / rx_gcd;
    unsigned long rx_decim = cfg.effective_rate / rx_gcd;
    log_info() << "RX resample:      " << cfg.effective_rate << " -> " << cfg.audio_rate
               << " (interp=" << rx_interp << ", decim=" << rx_decim << ")\n";
    if (rx_interp > 1000 || rx_decim > 1000) {
        log_error() << "RX resampler ratio " << rx_interp << "/" << rx_decim
                    << " too large (effective_rate=" << cfg.effective_rate
                    << ", audio_rate=" << cfg.audio_rate
                    << ") — choose rates with a reasonable GCD\n";
        return 1;
    }

    // Use integer sample count directly (passed from main, avoids float->int truncation)
    // iq_samples is at effective_rate (post-LPF). TX runs at sdr_rate.
    long long iq_samples = iq_sample_count;
    long long tx_samples = iq_samples * (long long)cfg.decimation;  // TX at sdr_rate
    double duration_sec = (double)iq_samples / cfg.effective_rate;
    // Divide before multiply to avoid overflow in the intermediate product.
    if (iq_samples % (long long)rx_decim != 0) {
        long long snapped = (iq_samples / (long long)rx_decim) * (long long)rx_decim;
        log_info() << "Snapping iq_samples " << iq_samples << " -> " << snapped
                   << " (multiple of rx_decim=" << rx_decim << ")\n";
        iq_samples = snapped;
        tx_samples = snapped * (long long)cfg.decimation;  // TX at sdr_rate
        if (iq_samples <= 0) {
            log_error() << "iq_samples snapped to 0 — duration too short for rx_decim\n";
            return 1;
        }
        duration_sec = (double)iq_samples / cfg.effective_rate;
    }
    long long rx_audio_samples = (iq_samples / (long long)rx_decim) * (long long)rx_interp;
    log_info() << "I/Q scale:        " << IQ_SCALE << " (|1.0| -> " << (int)IQ_SCALE << " int16)\n";
    log_info() << "TX samples:       " << tx_samples << " (at " << cfg.sdr_rate << " Hz)\n";
    log_info() << "RX I/Q samples:   " << iq_samples << " (at " << cfg.effective_rate << " Hz post-LPF)\n";
    log_info() << "RX audio samples: " << rx_audio_samples << " (at " << cfg.audio_rate << " Hz)\n";

    // Expected sc16 file size: 2 shorts per complex sample = 4 bytes/sample
    long long expected_iq_bytes = iq_samples * 4LL;
    log_info() << "Expected .sc16:   " << (double)expected_iq_bytes / (1024.0 * 1024.0) << " MiB\n";
    log_info() << "I/Q file:         " << iq_file << "\n";

    // Validate DSP parameters before designing filter taps
    // LPF cutoff is validated against sdr_rate (LPF input rate before decimation)
    if (cfg.lpf_cutoff <= 0 || cfg.lpf_cutoff >= cfg.sdr_rate / 2.0) {
        log_error() << "LPF cutoff " << cfg.lpf_cutoff << " Hz out of range (0, "
                    << cfg.sdr_rate / 2.0 << " Hz)\n";
        return 1;
    }
    if (cfg.lpf_transition <= 0) {
        log_error() << "LPF transition width must be > 0\n";
        return 1;
    }
    if (cfg.bandpass_low <= 0 || cfg.bandpass_high <= cfg.bandpass_low ||
        cfg.bandpass_high >= cfg.audio_rate / 2.0) {
        log_error() << "Bandpass range [" << cfg.bandpass_low << ", " << cfg.bandpass_high
                    << "] Hz invalid for audio rate " << cfg.audio_rate << " Hz\n";
        return 1;
    }

    // Design LPF taps at sdr_rate (input rate before decimation)
    std::vector<float> lpf_taps = gr::filter::firdes::low_pass(
        1.0, cfg.sdr_rate, cfg.lpf_cutoff, cfg.lpf_transition,
        gr::fft::window::WIN_HAMMING
    );
    log_info() << "LPF taps: " << lpf_taps.size()
               << " (designed at " << cfg.sdr_rate << " Hz, decim=" << cfg.decimation << ")\n";

    std::vector<float> bp_taps = gr::filter::firdes::band_pass(
        1.0, cfg.audio_rate, cfg.bandpass_low, cfg.bandpass_high, 200.0,
        gr::fft::window::WIN_HAMMING
    );
    log_info() << "Bandpass taps: " << bp_taps.size() << "\n";

    // --- Configure AD9361 via libiio ---
    // Write all attributes before building the flowgraph. The device_source/
    // device_sink iio_param_vec_t mechanism does not reliably apply attributes
    // on the flatsat (regression from fmcomms2_source/sink migration). Direct
    // libiio writes match the OPS-SAT SDR experimenter code template approach.
    {
        struct iio_context* cfg_ctx = iio_create_context_from_uri(cfg.uri.c_str());
        if (!cfg_ctx) {
            log_error() << "Could not connect to IIO for config at " << cfg.uri << "\n";
            return 1;
        }
        struct iio_device* phy = iio_context_find_device(cfg_ctx, "ad9361-phy");
        if (!phy) {
            log_error() << "No ad9361-phy device for config\n";
            iio_context_destroy(cfg_ctx);
            return 1;
        }
        if (!write_iio_rx_config(phy, (long long)cfg.frequency, (long long)cfg.sdr_rate,
                                 (long long)cfg.rf_bandwidth, cfg.rx_gain)) {
            iio_context_destroy(cfg_ctx);
            return 1;
        }
        if (cfg.enable_tx) {
            if (!write_iio_tx_config(phy, (long long)cfg.frequency, (long long)cfg.sdr_rate,
                                     (long long)cfg.rf_bandwidth, -cfg.tx_attenuation)) {
                iio_context_destroy(cfg_ctx);
                return 1;
            }
        }
        if (cfg.min_readback) {
            log_info() << "Minimal readback mode (--min-readback): sample rate mismatch is non-fatal\n";
        }
        if (!readback_iio_rx_config(phy, (long long)cfg.frequency, (long long)cfg.sdr_rate,
                                    (long long)cfg.rf_bandwidth, cfg.rx_gain,
                                    cfg.rate_tolerance, !cfg.min_readback)) {
            iio_context_destroy(cfg_ctx);
            return 1;
        }
        if (cfg.enable_tx) {
            if (!readback_iio_tx_config(phy, (long long)cfg.frequency,
                                        (long long)cfg.rf_bandwidth, cfg.tx_attenuation)) {
                iio_context_destroy(cfg_ctx);
                return 1;
            }
        }
        iio_context_destroy(cfg_ctx);
    }

    // --- Build flowgraph ---
    // Retry loop handles stale IIO connections from previously crashed runs.
    // When a process is killed by a signal, the IIO server (especially
    // network-based like iio-emu) may take time to detect the dead TCP connection
    // and free the connection slot. Retrying gives it time to recover.
    // On local IIO (real hardware), the first attempt always succeeds.
    gr::top_block_sptr tb;
    gr::blocks::head::sptr tx_head, iq_head, rx_head;

    const int MAX_BUILD_ATTEMPTS = 3;
    const int BUILD_RETRY_DELAY_SEC = 5;

    for (int attempt = 1; ; attempt++) {
    try {
        log_info() << "Building flowgraph...\n";
        tb = gr::make_top_block("loopback_test");

        // TX path blocks (only created when TX is enabled)
        gr::blocks::vector_source_f::sptr wav_src;
        gr::filter::rational_resampler_fff::sptr resampler_tx;
        gr::blocks::multiply_const_ff::sptr scaler;
        gr::analog::frequency_modulator_fc::sptr fm_mod;
        gr::blocks::complex_to_float::sptr from_fc32;
        gr::blocks::float_to_short::sptr tx_r_to_s16, tx_i_to_s16;
        gr::iio::device_sink::sptr iio_sink;

        if (cfg.enable_tx) {
            wav_src      = gr::blocks::vector_source_f::make(input_samples, false);
            resampler_tx = gr::filter::rational_resampler_fff::make(tx_interp, tx_decim);
            tx_head      = gr::blocks::head::make(sizeof(float), tx_samples);
            scaler       = gr::blocks::multiply_const_ff::make(0.8);
            double sensitivity = 2.0 * M_PI * cfg.fm_deviation / cfg.sdr_rate;
            fm_mod       = gr::analog::frequency_modulator_fc::make(sensitivity);

            // TX IIO sink — uses device_sink directly instead of fmcomms2_sink_fc32
            gr::iio::iio_param_vec_t no_tx_params;
            std::vector<std::string> tx_channels = {"voltage0", "voltage1"};
            bool cyclic_tx = (cfg.tx_mode == "cyclic");
            if (cyclic_tx) {
                log_info() << "Cyclic TX: DMA buffer will loop (no continuous CPU refill)\n";
            }
            iio_sink = gr::iio::device_sink::make(
                cfg.uri, "cf-ad9361-dds-core-lpc", tx_channels, "ad9361-phy",
                no_tx_params, cfg.iio_buffer_size, 0, cyclic_tx);

            // TX format conversion: gr_complex → float I/Q → int16
            from_fc32   = gr::blocks::complex_to_float::make(1);
            tx_r_to_s16 = gr::blocks::float_to_short::make(1, 32768.0f);
            tx_i_to_s16 = gr::blocks::float_to_short::make(1, 32768.0f);
        }

        // RX IIO source — same device_source approach as sdr-capture.
        // AD9361 attributes are written via direct libiio calls in
        // write_iio_config() before the build loop.
        gr::iio::iio_param_vec_t no_rx_params;
        std::vector<std::string> rx_channels = {"voltage0", "voltage1"};
        auto iio_src = gr::iio::device_source::make(
            cfg.uri, "cf-ad9361-lpc", rx_channels, "ad9361-phy",
            no_rx_params, cfg.iio_buffer_size);

        // RX format conversion: int16 → float → gr_complex
        // AD9361 ADC is 12-bit: divide by 2048.0 to normalize to ≈±1.0
        auto i_s2f   = gr::blocks::short_to_float::make(1, 2048.0f);
        auto q_s2f   = gr::blocks::short_to_float::make(1, 2048.0f);
        auto to_fc32 = gr::blocks::float_to_complex::make(1);

        // Config write + readback now happens before the build loop.

        // RX DSP blocks — LPF decimates from sdr_rate to effective_rate
        auto lpf         = gr::filter::fir_filter_ccf::make(cfg.decimation, lpf_taps);
        auto to_short    = gr::blocks::complex_to_interleaved_short::make(false, IQ_SCALE);
        iq_head          = gr::blocks::head::make(sizeof(gr_complex), iq_samples);
        // iq_samples counts complex samples at effective_rate (post-LPF);
        // sink writes 2 int16 per sample (I,Q) => 4 bytes/sample
        auto iq_sink_blk = gr::blocks::file_sink::make(sizeof(short), iq_file.c_str(), false);
        // FM demod operates at effective_rate (post-LPF output)
        auto fm_demod    = gr::analog::quadrature_demod_cf::make(
            cfg.effective_rate / (2.0 * M_PI * cfg.fm_deviation)
        );
        // Resample from effective_rate to audio_rate
        auto resampler_rx = gr::filter::rational_resampler_fff::make(rx_interp, rx_decim);
        auto bandpass     = gr::filter::fir_filter_fff::make(1, bp_taps);
        rx_head           = gr::blocks::head::make(sizeof(float), rx_audio_samples);
        auto wav_sink     = gr::blocks::wavfile_sink::make(
            cfg.output_wav.c_str(), 1, cfg.audio_rate,
            gr::blocks::FORMAT_WAV, gr::blocks::FORMAT_PCM_16
        );

        // --- Connect TX path (conditional on feature flags) ---
        if (cfg.enable_tx) {
            // wav → [resampler] → head → scaler → [FM mod] → convert → IIO sink
            gr::basic_block_sptr tx_prev = wav_src;
            if (cfg.enable_resampler_tx) {
                tb->connect(tx_prev, 0, resampler_tx, 0);
                tx_prev = resampler_tx;
            }
            tb->connect(tx_prev, 0, tx_head, 0);
            tb->connect(tx_head, 0, scaler, 0);
            if (cfg.enable_fm_mod) {
                tb->connect(scaler, 0, fm_mod, 0);
                tb->connect(fm_mod, 0, from_fc32, 0);
            } else {
                // No FM mod: wrap float audio into complex (Q=0) for IIO sink path
                auto passthru_f2c = gr::blocks::float_to_complex::make(1);
                tb->connect(scaler, 0, passthru_f2c, 0);
                tb->connect(passthru_f2c, 0, from_fc32, 0);
            }
            tb->connect(from_fc32, 0, tx_r_to_s16, 0);
            tb->connect(from_fc32, 1, tx_i_to_s16, 0);
            tb->connect(tx_r_to_s16, 0, iio_sink, 0);
            tb->connect(tx_i_to_s16, 0, iio_sink, 1);
        }

        // --- Connect RX path ---
        // IIO src (shorts) → fc32 conversion
        tb->connect(iio_src, 0, i_s2f, 0);
        tb->connect(iio_src, 1, q_s2f, 0);
        tb->connect(i_s2f, 0, to_fc32, 0);
        tb->connect(q_s2f, 0, to_fc32, 1);

        // [LPF] — complex → complex
        gr::basic_block_sptr rx_complex = to_fc32;
        if (cfg.enable_lpf) {
            tb->connect(rx_complex, 0, lpf, 0);
            rx_complex = lpf;
        }

        // Branch 1: sc16 file (always active)
        tb->connect(rx_complex, 0, iq_head, 0);
        tb->connect(iq_head, 0, to_short, 0);
        tb->connect(to_short, 0, iq_sink_blk, 0);

        // Branch 2: audio path — [FM demod] → [resampler] → [bandpass] → WAV
        gr::basic_block_sptr rx_audio;
        if (cfg.enable_fm_demod) {
            tb->connect(rx_complex, 0, fm_demod, 0);
            rx_audio = fm_demod;
        } else {
            // No FM demod: extract real part of complex as float for audio chain
            auto passthru_c2f = gr::blocks::complex_to_float::make(1);
            tb->connect(rx_complex, 0, passthru_c2f, 0);
            rx_audio = passthru_c2f;
        }
        if (cfg.enable_resampler_rx) {
            tb->connect(rx_audio, 0, resampler_rx, 0);
            rx_audio = resampler_rx;
        }
        if (cfg.enable_bandpass) {
            tb->connect(rx_audio, 0, bandpass, 0);
            rx_audio = bandpass;
        }
        tb->connect(rx_audio, 0, rx_head, 0);
        tb->connect(rx_head, 0, wav_sink, 0);

        // --- Start ---
        log_info() << "Starting flowgraph...\n";
        tb->start();
        break;  // flowgraph started — exit retry loop
    } catch (const std::exception& e) {
        // Release partial resources (including any IIO contexts held by GNU Radio blocks)
        tb.reset();
        tx_head.reset();
        iq_head.reset();
        rx_head.reset();

        if (attempt >= MAX_BUILD_ATTEMPTS) {
            log_error() << "FATAL: flowgraph build/start failed after "
                        << MAX_BUILD_ATTEMPTS << " attempts: " << e.what() << "\n";
            return 1;
        }
        log_warning() << "Flowgraph attempt " << attempt << "/" << MAX_BUILD_ATTEMPTS
                      << " failed: " << e.what()
                      << " — retrying in " << BUILD_RETRY_DELAY_SEC << "s\n";
        std::this_thread::sleep_for(std::chrono::seconds(BUILD_RETRY_DELAY_SEC));
    }
    } // end retry loop

    auto start_time = std::chrono::steady_clock::now();
    int timeout_sec = (int)(duration_sec * cfg.timeout_multiplier) + 10;
    log_info() << "Timeout:  " << timeout_sec << " seconds ("
               << cfg.timeout_multiplier << "x duration + 10)\n";

    bool timed_out = false;
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        auto elapsed = std::chrono::steady_clock::now() - start_time;
        auto elapsed_sec = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();

        bool iq_done = iq_head->nitems_written(0) >= (uint64_t)iq_samples;
        if (iq_done) {
            // Grace period: let the scheduler drain the I/Q pipeline
            // (head → complex_to_interleaved_short → file_sink) before we
            // call tb->stop().  Without this, in-flight buffer items could
            // be lost, producing a slightly short sc16 file.
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            log_info() << "Capture complete (TX: " << (tx_head ? tx_head->nitems_written(0) : 0) << "/" << tx_samples
                       << ", RX audio: " << rx_head->nitems_written(0) << "/" << rx_audio_samples
                       << ", RX I/Q: " << iq_head->nitems_written(0) << "/" << iq_samples
                       << " samples, " << elapsed_sec << "s)\n";
            break;
        }

        // Progress every 5 seconds
        if (elapsed_sec > 0 && elapsed_sec % 5 == 0 &&
            std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() % 5000 < 100) {
            log_info() << "Progress [" << elapsed_sec << "s]: TX "
                       << (tx_head ? tx_head->nitems_written(0) : 0) << "/" << tx_samples
                       << ", I/Q " << iq_head->nitems_written(0) << "/" << iq_samples
                       << ", audio " << rx_head->nitems_written(0) << "/" << rx_audio_samples << "\n";
        }

        if (elapsed >= std::chrono::seconds(timeout_sec)) {
            log_error() << "Timeout after " << elapsed_sec << " seconds"
                        << " (TX: " << (tx_head ? tx_head->nitems_written(0) : 0) << "/" << tx_samples
                        << ", RX audio: " << rx_head->nitems_written(0) << "/" << rx_audio_samples
                        << ", RX I/Q: " << iq_head->nitems_written(0) << "/" << iq_samples << ")\n";
            timed_out = true;
            tb->stop();  // belt-and-suspenders: stop immediately so wait() won't hang on wedged graph
            break;
        }
    }

    bool interrupted = (g_signal_received != 0);
    if (interrupted) {
        log_info() << "Received signal " << g_signal_received << ", stopping...\n";
    }
    if (timed_out) {
        log_error() << "Loopback may have failed - check SDR connection\n";
    }

    log_info() << "Stopping flowgraph...\n";
    tb->stop();

    // Watchdog: run tb->wait() on a thread with a grace period.
    // If wait() hangs (wedged IIO driver, stuck DMA), force-exit to prevent
    // mission automation scripts from stalling indefinitely.
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
            auto wait_elapsed = std::chrono::steady_clock::now() - wait_start;
            if (wait_elapsed >= std::chrono::seconds(WAIT_GRACE_SEC)) {
                log_error() << "FATAL: tb->wait() hung for " << WAIT_GRACE_SEC
                            << "s after stop — force-exiting to prevent automation stall\n";
                // Best-effort loopback restore before forced exit (_Exit skips RAII)
                restore_loopback(cfg.uri, prev_loopback);
                std::cerr.flush();
                std::cout.flush();
                wait_thread.detach();
                _Exit(2);
            }
        }
        wait_thread.join();
    }

    auto end_time = std::chrono::steady_clock::now();
    auto total_sec = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time).count();

    bool early_stop = timed_out || interrupted;
    long long actual_tx_written = (long long)(tx_head ? tx_head->nitems_written(0) : 0);
    long long actual_iq_written = (long long)iq_head->nitems_written(0);
    long long actual_audio_written = (long long)rx_head->nitems_written(0);

    log_info() << (early_stop ? "Loopback stopped early" : "Loopback test complete") << "\n";
    log_info() << "Wall time: " << total_sec << " seconds\n";
    log_info() << "TX:    " << actual_tx_written << "/" << tx_samples << " samples\n";
    log_info() << "Audio: " << cfg.output_wav << "\n";
    log_info() << "I/Q:   " << iq_file << "\n";
    if (early_stop) {
        log_info() << "Partial:   TX " << actual_tx_written << "/" << tx_samples
                   << ", I/Q " << actual_iq_written << "/" << iq_samples
                   << ", audio " << actual_audio_written << "/" << rx_audio_samples << "\n";
        // Use libsndfile as authoritative source for actual WAV frame count
        SF_INFO sf_check = {0};
        SNDFILE* sf_partial = sf_open(cfg.output_wav.c_str(), SFM_READ, &sf_check);
        if (sf_partial) {
            double wav_duration = (sf_check.samplerate > 0)
                ? (double)sf_check.frames / sf_check.samplerate : 0.0;
            log_info() << "WAV file:  " << sf_check.frames << " frames @ "
                       << sf_check.samplerate << " Hz (" << wav_duration << "s, partial)\n";
            sf_close(sf_partial);
        } else {
            log_warning() << "could not open WAV for partial frame count check\n";
        }
    }

    // Track hard validation failures (logged as FAIL but must also affect exit code)
    bool hard_fail = false;

    // Post-processing: RMS normalization to -20 dBFS (skip on partial capture)
    if (early_stop) {
        log_warning() << "skipping audio normalization (partial capture)\n";
    } else {
        log_info() << "Normalizing audio...\n";
        if (!rms_normalize(cfg.output_wav, -20.0)) {
            log_warning() << "audio normalization failed\n";
        }
    }

    // Verify sc16 file — file size on disk is the authoritative source of truth
    {
        std::ifstream iq_check(iq_file, std::ios::binary | std::ios::ate);
        if (iq_check.is_open()) {
            long long actual_bytes = (long long)iq_check.tellg();
            if (actual_bytes < 0) {
                log_warning() << "sc16 file size check: tellg() failed\n";
            } else if (actual_bytes == 0) {
                log_error() << "FAIL: sc16 file is empty\n";
                hard_fail = true;
            } else {
                if (actual_bytes % 4 != 0) {
                    log_warning() << "sc16 file size " << actual_bytes
                                  << " not a multiple of 4 bytes (truncated write?)\n";
                }
                long long actual_samples = actual_bytes / 4;
                double actual_duration = (double)actual_samples / cfg.effective_rate;
                if (early_stop) {
                    // On early stop, just report what's on disk — don't compare to expected
                    log_info() << "sc16 file: " << actual_bytes << " bytes, "
                               << actual_samples << " samples ("
                               << actual_duration << "s, partial)\n";
                } else {
                    long long expected_bytes = iq_samples * 4LL;
                    log_info() << "sc16 file size: " << actual_bytes << " bytes"
                               << " (expected " << expected_bytes << ")\n";
                    if (expected_bytes <= 0) {
                        log_warning() << "expected_bytes is 0, skipping deviation check\n";
                    } else if (actual_bytes != expected_bytes) {
                        double deviation = std::abs((double)(actual_bytes - expected_bytes) / expected_bytes);
                        if (deviation > 0.05) {
                            log_error() << "FAIL: sc16 size deviation " << (deviation * 100.0)
                                        << "% — got " << actual_samples << " samples ("
                                        << actual_duration << "s)\n";
                            hard_fail = true;
                        } else if (deviation > 0.005) {
                            log_warning() << "sc16 size deviation " << (deviation * 100.0)
                                        << "% — got " << actual_samples << " samples ("
                                        << actual_duration << "s)\n";
                        }
                    }
                }
            }
        } else {
            log_error() << "FAIL: Could not open sc16 file: " << iq_file << "\n";
            hard_fail = true;
        }
    }

    // Check for I/Q clipping and peak magnitude (still useful on partial captures)
    double clip_rate;
    int peak_abs;
    check_sc16_quality(iq_file, (long long)cfg.effective_rate, clip_rate, peak_abs);
    if (clip_rate >= 0.0) {
        double peak_dbfs = (peak_abs > 0) ? 20.0 * std::log10((double)peak_abs / IQ_SCALE) : -999.0;
        log_info() << "sc16 peak: " << peak_abs << "/" << (int)IQ_SCALE
                   << " (" << peak_dbfs << " dBFS@IQ_SCALE)\n";
        log_info() << "sc16 rail hit rate: " << (clip_rate * 100.0) << "% (per int16, ±32767)\n";
        if (clip_rate > 0.01) {
            log_warning() << "sc16 rail hit rate " << (clip_rate * 100.0)
                        << "% exceeds 1% — RX may be saturating "
                        << "(reduce TX level or RX gain)\n";
        }
    }

    // I/Q diagnostic statistics: RMS, DC offset, min/max, zero fraction
    {
        Sc16Stats iq_stats = analyze_sc16(iq_file);
        if (iq_stats.valid) {
            double rms_i_db = (iq_stats.rms_i > 0) ? 20.0 * std::log10(iq_stats.rms_i / IQ_SCALE) : -999.0;
            double rms_q_db = (iq_stats.rms_q > 0) ? 20.0 * std::log10(iq_stats.rms_q / IQ_SCALE) : -999.0;
            log_info() << "IQ diag:  " << iq_stats.samples << " samples analyzed\n";
            log_info() << "IQ RMS:   I=" << iq_stats.rms_i << " (" << rms_i_db << " dBFS)"
                       << "  Q=" << iq_stats.rms_q << " (" << rms_q_db << " dBFS)\n";
            log_info() << "IQ DC:    I=" << iq_stats.mean_i << "  Q=" << iq_stats.mean_q << "\n";
            log_info() << "IQ range: I=[" << iq_stats.min_i << "," << iq_stats.max_i << "]"
                       << "  Q=[" << iq_stats.min_q << "," << iq_stats.max_q << "]\n";
            double zero_pct_i = 100.0 * iq_stats.zero_i / iq_stats.samples;
            double zero_pct_q = 100.0 * iq_stats.zero_q / iq_stats.samples;
            log_info() << "IQ zeros: I=" << zero_pct_i << "%  Q=" << zero_pct_q << "%\n";

            // Flag dead signal (all zeros = loopback not connected)
            if (iq_stats.rms_i < 1.0 && iq_stats.rms_q < 1.0) {
                log_error() << "FAIL: IQ signal is dead (RMS < 1 on both channels) "
                            << "— loopback may not be routing TX to RX\n";
                hard_fail = true;
            }
            // Flag I/Q imbalance (more than 6 dB difference)
            if (iq_stats.rms_i > 1.0 && iq_stats.rms_q > 1.0) {
                double balance_db = std::abs(rms_i_db - rms_q_db);
                if (balance_db > 6.0) {
                    log_warning() << "IQ imbalance: " << balance_db
                                  << " dB between I and Q channels\n";
                }
            }
        }
    }

    // Generate spectrogram thumbnail for downlink triage
    if (cfg.enable_spectrogram) {
        std::string spec_file = make_spectrogram_filename(iq_file);
        if (!generate_spectrogram(iq_file, spec_file, (long long)cfg.effective_rate)) {
            log_warning() << "spectrogram generation failed\n";
        }
    }

    // Generate constellation plot for I/Q health check (Q dropout detection)
    if (cfg.enable_constellation) {
        std::string const_file = make_constellation_filename(iq_file);
        if (!generate_constellation(iq_file, const_file)) {
            log_warning() << "constellation generation failed\n";
        }
    }

    // Loopback signal quality validation: cross-correlate input and output audio
    if (!early_stop) {
        double lag_ms = 0.0;
        double correlation = loopback_quality_check(cfg.input_wav, cfg.output_wav, lag_ms);
        if (correlation < 0.0) {
            log_warning() << "could not compute loopback quality metric\n";
        } else {
            log_info() << "Loopback quality: correlation=" << correlation
                       << ", lag=" << lag_ms << " ms\n";
            if (correlation < 0.3) {
                log_error() << "FAIL: loopback correlation " << correlation
                            << " < 0.3 — output does not resemble input "
                            << "(loopback may not be working)\n";
                hard_fail = true;
            } else if (correlation < 0.7) {
                log_warning() << "loopback correlation " << correlation
                            << " < 0.7 — marginal signal quality\n";
            } else {
                log_info() << "Loopback PASS: correlation " << correlation << " >= 0.7\n";
            }
        }
    } else {
        log_warning() << "skipping loopback quality check (partial capture)\n";
    }

    log_info() << "NOTE: SDR at " << cfg.sdr_rate / 1000 << " kSPS, decimation "
               << cfg.decimation << "x -> " << cfg.effective_rate / 1000 << " kSPS effective\n";

    if (timed_out) return 2;
    if (interrupted) return 128 + g_signal_received;  // conventional: 130 for SIGINT, 143 for SIGTERM
    if (hard_fail) return 1;
    return 0;
}

#endif // LOOPBACK_FLOWGRAPH_H
