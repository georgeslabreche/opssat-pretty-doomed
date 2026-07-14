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
#include <gnuradio/blocks/file_source.h>
#include <gnuradio/blocks/interleaved_short_to_complex.h>
#include <gnuradio/blocks/rotator_cc.h>
#include <gnuradio/blocks/wavfile_sink.h>
#include <gnuradio/iio/device_source.h>

#include "pretty_log.h"
#include "pretty_signal.h"
#include "pretty_audio.h"
#include "pretty_spectrogram.h"
#include "pretty_constellation.h"

#include "chain.h"
#include "sdr.h"

using namespace pretty;

// I/Q artifact generation (diagnostics, metrics, spectrogram, constellation,
// PSD), shared by the live capture and the sc16 replay input (#116).
static void generate_iq_artifacts(const PipelineConfig& cfg,
                                  const std::string& iq_file,
                                  unsigned long effective_rate) {
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

            std::string metrics_file = make_metrics_filename(iq_file);
            if (write_sc16_metrics(metrics_file, iq_stats, IQ_SCALE)) {
                log_info() << "IQ metrics: " << metrics_file << "\n";
            }
        }

        if (cfg.sdr_enable_spectrogram) {
            std::string spec_file = make_spectrogram_filename(iq_file);
            log_info() << "Spectrogram: " << spec_file << "\n";
            if (!generate_spectrogram(iq_file, spec_file, (long long)effective_rate)) {
                log_warning() << "Spectrogram generation failed\n";
            }
        }
        if (cfg.sdr_enable_constellation) {
            std::string const_file = make_constellation_filename(iq_file);
            log_info() << "Constellation: " << const_file << "\n";
            if (!generate_constellation(iq_file, const_file)) {
                log_warning() << "Constellation generation failed\n";
            }
        }
        if (cfg.sdr_enable_psd) {
            std::string psd_csv = make_psd_csv_filename(iq_file);
            std::string psd_bmp = make_psd_bmp_filename(iq_file);
            log_info() << "PSD: " << psd_csv << "\n";
            if (!generate_psd(iq_file, psd_csv, psd_bmp, (long long)effective_rate)) {
                log_warning() << "PSD generation failed\n";
            }
        }
}

// #111: regenerate the WAV from the just-written sc16 with the narrowing
// stage: peak search around DC, shift the found peak to DC, band-limit to
// +/-sdr_narrow_bw/2 and decimate before the unchanged demod chain (the same
// shared blocks the streaming flowgraph uses, #112). The streaming flowgraph
// is untouched; this replaces capture.wav before normalization. Returns false
// (keeping the wide audio) on any failure.
static bool narrow_rewrite_wav(const PipelineConfig& cfg,
                               const std::string& iq_file,
                               const std::string& wav_file,
                               unsigned long effective_rate) {
    try {
        ChainParams np = compute_chain_params(cfg, cfg.sdr_narrow_bw);
        if (!np.valid || np.narrow_decim == 0) {
            log_warning() << "Narrowing: invalid parameters, keeping wide audio\n";
            return false;
        }

        // The AD9361 DC spike sits at 0 Hz, where the uplink is also expected
        // operationally; exclude a small guard so the spike cannot win.
        const double DC_GUARD_HZ = 2000.0;
        double f_peak = find_peak_offset(iq_file, (double)effective_rate, 0.0,
                                         cfg.sdr_narrow_search, DC_GUARD_HZ);
        log_info() << "Narrowing: peak at " << f_peak / 1e3
                   << " kHz from center; regenerating audio (+/-"
                   << cfg.sdr_narrow_bw / 2e3 << " kHz, "
                   << np.disc_rate << " Hz at the discriminator)\n";

        auto tb = gr::make_top_block("narrow_rewrite");
        auto src = gr::blocks::file_source::make(sizeof(short), iq_file.c_str(), false);
        auto s2c = gr::blocks::interleaved_short_to_complex::make();
        auto rot = gr::blocks::rotator_cc::make(
            -2.0 * M_PI * f_peak / (double)effective_rate);
        AudioChain chain = make_audio_chain(np, cfg);
        auto wav_sink = gr::blocks::wavfile_sink::make(
            wav_file.c_str(), 1, cfg.sdr_audio_rate,
            gr::blocks::FORMAT_WAV, gr::blocks::FORMAT_PCM_16);

        tb->connect(src, 0, s2c, 0);
        tb->connect(s2c, 0, rot, 0);
        tb->connect(rot, 0, chain.narrow_lpf, 0);
        connect_audio_chain_from_baseband(tb, chain, wav_sink);
        tb->run();
        tb.reset();  // flush and close the WAV
        return true;
    } catch (const std::exception& e) {
        log_warning() << "Narrowing failed (" << e.what()
                      << "), keeping wide audio\n";
        return false;
    }
}

bool run_capture(const PipelineConfig& cfg,
                     const std::string& output_dir,
                     CaptureResult& result) {
    result.success = false;

    // Validate hardware FIR parameters before deriving rates from them
    if (cfg.sdr_hw_fir_enable) {
        if (cfg.sdr_hw_fir_rate <= 0 || cfg.sdr_hw_fir_fpass <= 0 ||
            cfg.sdr_hw_fir_fstop <= 0 || cfg.sdr_hw_fir_wnom_tx <= 0 ||
            cfg.sdr_hw_fir_wnom_rx <= 0) {
            log_error() << "Hardware FIR enabled but parameters are missing or invalid\n";
            return false;
        }
    }

    // Shared audio-chain parameters (#112): rates, resampler ratios, and
    // filter taps, derived exactly as this file historically derived them.
    // The ground preview tool (tools/preview_onboard) uses the same module, so
    // preview and flight execute the same chain by construction. Narrowing
    // stays disabled here until the config-gated fold-in (#111).
    ChainParams params = compute_chain_params(cfg);
    if (!params.valid) {
        return false;
    }

    // Output paths
    std::string wav_file = output_dir + "/capture.wav";
    std::string iq_file = output_dir + "/capture.sc16";

    // Sample counts
    long long iq_samples = (long long)(cfg.sdr_duration * (double)params.effective_rate);

    // Cap to downlink budget
    const long long max_iq_bytes = (long long)cfg.sdr_max_iq_mb * 1024LL * 1024;
    long long max_iq_samples = max_iq_bytes / 4;
    if (iq_samples > max_iq_samples) {
        log_warning() << "Capping capture to " << cfg.sdr_max_iq_mb << " MiB ("
                      << max_iq_samples << " samples)\n";
        iq_samples = max_iq_samples;
    }

    // Snap to resampler decimation multiple
    {
        long long rem = iq_samples % (long long)params.decimation;
        if (rem != 0) iq_samples -= rem;
    }
    if (iq_samples <= 0) {
        log_error() << "Capture duration too short\n";
        return false;
    }

    long long audio_samples = (iq_samples / (long long)params.decimation) * (long long)params.interpolation;
    double duration_sec = (double)iq_samples / params.effective_rate;

    log_info() << "Configuring SDR...\n";
    log_info() << "SDR Capture: " << duration_sec << "s at " << params.effective_rate << " Hz effective\n";
    log_info() << "  Frequency:   " << cfg.sdr_frequency / 1e6 << " MHz\n";
    if (cfg.sdr_hw_fir_enable) {
        log_info() << "  HW FIR:      " << cfg.sdr_rate << " Hz ADC -> "
                   << cfg.sdr_hw_fir_rate << " Hz post-FIR\n";
        log_info() << "  SDR rate:    " << params.gnuradio_input_rate << " Hz (post-FIR), decimation "
                   << cfg.sdr_decimation << "x\n";
    } else {
        log_info() << "  SDR rate:    " << cfg.sdr_rate << " Hz, decimation "
                   << cfg.sdr_decimation << "x\n";
    }
    log_info() << "  I/Q samples: " << iq_samples << ", audio samples: " << audio_samples << "\n";
    log_info() << "  Resample:    " << params.effective_rate << " -> " << cfg.sdr_audio_rate
               << " (interp=" << params.interpolation << ", decim=" << params.decimation << ")\n";
    log_info() << "  LPF taps: " << params.lpf_taps.size() << ", bandpass taps: " << params.bp_taps.size() << "\n";

    // Configure AD9361 if running in per-capture init mode.
    // Default (sdr_init_per_capture=false): init is done once before the capture
    // loop in main.cpp. Per-capture mode re-inits from scratch each time as a
    // defensive measure against IIO driver state issues after failed captures.
    if (cfg.sdr_init_per_capture) {
        log_info() << "Per-capture AD9361 init (robust mode)\n";
        if (!ad9361_configure(cfg)) {
            return false;
        }
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

        // Shared audio chain (#112): channel LPF -> FM demod -> resampler ->
        // voice band-pass, identical to the ground preview tool by construction.
        AudioChain chain = make_audio_chain(params, cfg);

        auto to_short  = gr::blocks::complex_to_interleaved_short::make(false, IQ_SCALE);
        iq_head        = gr::blocks::head::make(sizeof(gr_complex), iq_samples);
        auto iq_sink   = gr::blocks::file_sink::make(sizeof(short), iq_file.c_str(), false);
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
        tb->connect(to_fc32, 0, chain.lpf, 0);

        // Branch 1: sc16 file, tapped off the channel LPF
        tb->connect(chain.lpf, 0, iq_head, 0);
        tb->connect(iq_head, 0, to_short, 0);
        tb->connect(to_short, 0, iq_sink, 0);

        // Branch 2: FM demod -> audio WAV
        connect_audio_chain(tb, chain, audio_head, wav_sink);

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
    int timeout_sec = (int)(cfg.sdr_duration * cfg.sdr_timeout_multiplier) + 10;
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

    // #111: config-gated narrowing, entirely post-capture. Runs on whatever
    // the sc16 holds (including partial captures); sdr_narrow_enable=false or
    // a missing key keeps the chain exactly as flown.
    if (cfg.sdr_narrow_enable) {
        narrow_rewrite_wav(cfg, iq_file, wav_file, params.effective_rate);
    }

    // I/Q artifact generation (shared with the sc16 replay input, #116)
    auto generate_artifacts = [&cfg, iq_file, effective_rate = params.effective_rate]() {
        generate_iq_artifacts(cfg, iq_file, effective_rate);
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

bool run_capture_from_file(const PipelineConfig& cfg,
                           const std::string& sc16_input,
                           const std::string& output_dir,
                           CaptureResult& result) {
    result.success = false;

    ChainParams params = compute_chain_params(cfg);
    if (!params.valid) {
        return false;
    }

    // Copy the input into place as capture.sc16 so downstream semantics are
    // identical to a live capture (artifacts derive from it, and
    // sdr_keep_sc16=false may delete it after processing).
    std::string wav_file = output_dir + "/capture.wav";
    std::string iq_file = output_dir + "/capture.sc16";
    {
        std::ifstream src(sc16_input, std::ios::binary);
        if (!src) {
            log_error() << "sc16 input not readable: " << sc16_input << "\n";
            return false;
        }
        std::ofstream dst(iq_file, std::ios::binary);
        dst << src.rdbuf();
        if (!dst) {
            log_error() << "failed to write " << iq_file << "\n";
            return false;
        }
    }

    long long n_bytes = 0;
    {
        std::ifstream f(iq_file, std::ios::binary | std::ios::ate);
        n_bytes = (long long)f.tellg();
    }
    double duration_sec = (double)(n_bytes / 4) / params.effective_rate;
    log_info() << "SC16 replay: " << sc16_input << " (" << duration_sec
               << "s at " << params.effective_rate << " Hz effective)\n";

    try {
        // Wide demod of the baseband, matching the live audio branch: the
        // sc16 is post-channel-LPF, so the chain enters at the discriminator.
        auto tb = gr::make_top_block("sc16_replay");
        auto src = gr::blocks::file_source::make(sizeof(short), iq_file.c_str(), false);
        auto s2c = gr::blocks::interleaved_short_to_complex::make();
        AudioChain chain = make_audio_chain(params, cfg);
        auto wav_sink = gr::blocks::wavfile_sink::make(
            wav_file.c_str(), 1, cfg.sdr_audio_rate,
            gr::blocks::FORMAT_WAV, gr::blocks::FORMAT_PCM_16);
        tb->connect(src, 0, s2c, 0);
        tb->connect(s2c, 0, chain.fm_demod, 0);
        tb->connect(chain.fm_demod, 0, chain.resampler, 0);
        tb->connect(chain.resampler, 0, chain.bandpass, 0);
        tb->connect(chain.bandpass, 0, wav_sink, 0);
        tb->run();
        tb.reset();
    } catch (const std::exception& e) {
        log_error() << "sc16 replay flowgraph failed: " << e.what() << "\n";
        return false;
    }

    // Identical post-capture stages as a live capture.
    if (cfg.sdr_narrow_enable) {
        narrow_rewrite_wav(cfg, iq_file, wav_file, params.effective_rate);
    }

    log_info() << "Normalizing audio...\n";
    if (!rms_normalize(wav_file, -20.0)) {
        log_warning() << "Audio normalization failed\n";
    }
    generate_iq_artifacts(cfg, iq_file, params.effective_rate);

    result.wav_path = wav_file;
    result.sc16_path = iq_file;
    result.duration_sec = duration_sec;
    result.success = true;
    log_info() << "SC16 replay OK\n";
    return true;
}
