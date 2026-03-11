/*
 * capture_loop.cpp - GNU Radio IIO RF Capture for OPS-SAT PRETTY
 *
 * Continuous RX capture loop for live RF reception.
 * Captures FM-demodulated audio and sc16 I/Q from AD9361 SDR.
 *
 * DSP chain:
 *   IIO src (fc32, sdr_rate e.g. 2.4 MSPS)
 *     -> Decimating LPF (85 kHz cutoff, decim=12 -> 200 kSPS effective)
 *       +-> head -> complex_to_interleaved_short -> file_sink (.sc16)
 *       +-> FM demod -> rational_resampler (200k->16k)
 *           -> bandpass (300-3400 Hz) -> head -> wav_sink
 *   Post-processing: RMS normalization to -20 dBFS
 *
 * Usage: capture_loop -o output.wav -d duration_seconds [-c config.cfg]
 */

#include <algorithm>
#include <iostream>
#include <fstream>
#include <string>
#include <chrono>
#include <thread>
#include <atomic>
#include <cmath>
#include <csignal>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <vector>

#include <gnuradio/top_block.h>
#include <gnuradio/blocks/wavfile_sink.h>
#include <gnuradio/blocks/file_sink.h>
#include <gnuradio/blocks/head.h>
#include <gnuradio/blocks/short_to_float.h>
#include <gnuradio/blocks/float_to_complex.h>
#include <gnuradio/blocks/complex_to_interleaved_short.h>
#include <gnuradio/analog/quadrature_demod_cf.h>
#include <gnuradio/filter/rational_resampler.h>
#include <gnuradio/filter/fir_filter_blk.h>
#include <gnuradio/filter/firdes.h>
#include <gnuradio/iio/device_source.h>
#include <numeric>  // for std::gcd

#include <sched.h>

#include <iio.h>
#include <sndfile.h>

#include "pretty_log.h"
#include "pretty_signal.h"
#include "pretty_config.h"
#include "pretty_iio.h"
#include "pretty_audio.h"
#include "pretty_spectrogram.h"

using namespace pretty;

// Defaults are overridden by config file values (-c), then by command-line args.
struct CaptureConfig {
    // SDR hardware
    long long frequency = 1296000000;  // overridden by config: frequency
    long sdr_rate = 2400000;           // overridden by config: sdr_rate (AD9361 hardware rate)
    int decimation = 12;               // overridden by config: decimation (LPF decimation factor)
    long rf_bandwidth = 200000;        // overridden by config: rf_bandwidth (AD9361 analog filter)
    double gain = 50.0;               // overridden by config: gain
    double fm_deviation = 5000.0;     // overridden by config: fm_deviation
    std::string uri = "local:";       // overridden by config: uri

    // Derived (set after parsing)
    long effective_rate = 200000;      // sdr_rate / decimation

    // Capture
    int duration = 20;                // overridden by config: duration
    int max_iq_mb = 20;              // overridden by config: max_iq_mb

    // Audio
    int audio_rate = 16000;           // overridden by config: audio_rate
    double bandpass_low = 300.0;      // overridden by config: bandpass_low
    double bandpass_high = 3400.0;    // overridden by config: bandpass_high

    // LPF
    double lpf_cutoff = 85000.0;     // overridden by config: lpf_cutoff
    double lpf_transition = 15000.0; // overridden by config: lpf_transition

    // Output
    std::string output_wav = "capture.wav";

    // Testing / diagnostics
    bool min_readback = false;          // --min-readback: downgrade sample rate check to warning (emulator)
    bool single_core = false;           // --single-core: pin process to CPU 0 (diagnose threading issues)
    long rate_tolerance = 10;           // max Hz offset for sample rate readback before fatal (AD9361 PLL quantization)
    int timeout_multiplier = 5;         // config: timeout_multiplier — timeout = duration * N + 10 (default 5 for ARM CPU headroom)
};

bool load_config(const std::string& path, CaptureConfig& cfg) {
    auto result = load_config_map(path);
    if (!result.ok) return false;

    for (const auto& [key, value] : result.values) {
        try {
            if (key == "frequency") cfg.frequency = std::stoll(value);
            else if (key == "sdr_rate") cfg.sdr_rate = std::stol(value);
            else if (key == "decimation") cfg.decimation = std::stoi(value);
            else if (key == "rf_bandwidth") cfg.rf_bandwidth = std::stol(value);
            else if (key == "gain") cfg.gain = std::stod(value);
            else if (key == "fm_deviation") cfg.fm_deviation = std::stod(value);
            else if (key == "uri") cfg.uri = value;
            else if (key == "duration") cfg.duration = std::stoi(value);
            else if (key == "max_iq_mb") cfg.max_iq_mb = std::stoi(value);
            else if (key == "audio_rate") cfg.audio_rate = std::stoi(value);
            else if (key == "bandpass_low") cfg.bandpass_low = std::stod(value);
            else if (key == "bandpass_high") cfg.bandpass_high = std::stod(value);
            else if (key == "lpf_cutoff") cfg.lpf_cutoff = std::stod(value);
            else if (key == "lpf_transition") cfg.lpf_transition = std::stod(value);
            else if (key == "min_readback") cfg.min_readback = (value == "true" || value == "1");
            else if (key == "single_core") cfg.single_core = (value == "true" || value == "1");
            else if (key == "rate_tolerance") cfg.rate_tolerance = std::stol(value);
            else if (key == "timeout_multiplier") cfg.timeout_multiplier = std::stoi(value);
            else if (key == "captures") { /* consumed by run script */ }
            else log_warning() << path << ": unknown config key: " << key << "\n";
        } catch (const std::exception& e) {
            log_error() << path << ": parse error: " << key << "=" << value
                        << " (" << e.what() << ")\n";
            return false;
        }
    }

    return true;
}

void print_usage(const char* prog) {
    std::cout << "OPS-SAT SDR RF Capture\n"
              << "======================\n\n"
              << "Usage: " << prog << " [options]\n\n"
              << "Options:\n"
              << "  -c, --config    Config file path (KEY=VALUE format)\n"
              << "  -o, --output    Output WAV file (default: capture.wav)\n"
              << "  -d, --duration  Capture duration in seconds (default: 20)\n"
              << "  -u, --uri       IIO URI (default: local:)\n"
              << "  -f, --freq      Frequency in Hz (default: 1296000000)\n"
              << "  -g, --gain      RX gain in dB (default: 50)\n"
              << "  -e, --deviation FM deviation in Hz (default: 5000)\n"
              << "      --min-readback  Minimal readback: downgrade sample rate check to warning (emulator)\n"
              << "      --single-core   Pin process to CPU 0 (diagnose multi-threading crashes)\n"
              << "  -h, --help      Show this help\n";
}

// Parse config file (-c) then command-line overrides. Returns 0 on success,
// 1 on error, 2 if --help was requested (caller should exit 0).
int parse_args(int argc, char* argv[], CaptureConfig& cfg) {
    std::string config_path;

    // First pass: find config file
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if ((arg == "-c" || arg == "--config") && i + 1 < argc) {
            config_path = argv[++i];
        }
    }

    // Load config file if specified
    if (!config_path.empty()) {
        if (!load_config(config_path, cfg)) {
            return 1;
        }
    }

    // Second pass: command-line overrides
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if ((arg == "-c" || arg == "--config") && i + 1 < argc) {
            i++;  // already handled
        } else if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            cfg.output_wav = argv[++i];
        } else if ((arg == "-d" || arg == "--duration") && i + 1 < argc) {
            cfg.duration = std::stoi(argv[++i]);
        } else if ((arg == "-u" || arg == "--uri") && i + 1 < argc) {
            cfg.uri = argv[++i];
        } else if ((arg == "-f" || arg == "--freq") && i + 1 < argc) {
            cfg.frequency = std::stoll(argv[++i]);
        } else if ((arg == "-g" || arg == "--gain") && i + 1 < argc) {
            cfg.gain = std::stod(argv[++i]);
        } else if ((arg == "-e" || arg == "--deviation") && i + 1 < argc) {
            cfg.fm_deviation = std::stod(argv[++i]);
            if (cfg.fm_deviation <= 0) {
                log_error() << "FM deviation must be > 0\n";
                return 1;
            }
        } else if (arg == "--min-readback") {
            cfg.min_readback = true;
        } else if (arg == "--single-core") {
            cfg.single_core = true;
        } else if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 2;
        } else if (arg[0] == '-') {
            log_error() << "Unknown option: " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    // Validate
    if (cfg.duration <= 0) {
        log_error() << "Duration must be > 0\n";
        return 1;
    }
    if (cfg.sdr_rate <= 0) {
        log_error() << "sdr_rate must be > 0\n";
        return 1;
    }
    if (cfg.decimation <= 0) {
        log_error() << "decimation must be > 0\n";
        return 1;
    }
    if (cfg.sdr_rate % cfg.decimation != 0) {
        log_error() << "sdr_rate (" << cfg.sdr_rate << ") must be divisible by decimation ("
                    << cfg.decimation << ")\n";
        return 1;
    }
    cfg.effective_rate = cfg.sdr_rate / cfg.decimation;
    if (cfg.effective_rate <= 0) {
        log_error() << "effective_rate (sdr_rate/decimation) must be > 0\n";
        return 1;
    }
    // AD9361 sampling_frequency_Hz limits: 2,083,000 - 61,440,000 Hz
    if (cfg.sdr_rate < 2083000 || cfg.sdr_rate > 61440000) {
        log_error() << "sdr_rate " << cfg.sdr_rate
                    << " Hz out of AD9361 range (2,083,000 - 61,440,000 Hz)\n";
        return 1;
    }
    // AD9361 rf_bandwidth_Hz limits: 200,000 - 56,000,000 Hz
    if (cfg.rf_bandwidth < 200000 || cfg.rf_bandwidth > 56000000) {
        log_error() << "rf_bandwidth " << cfg.rf_bandwidth
                    << " Hz out of AD9361 range (200,000 - 56,000,000 Hz)\n";
        return 1;
    }
    if (cfg.max_iq_mb <= 0) {
        log_error() << "max_iq_mb must be > 0\n";
        return 1;
    }
    if (cfg.audio_rate <= 0) {
        log_error() << "audio_rate must be > 0\n";
        return 1;
    }

    return 0;
}

void log_config(const CaptureConfig& cfg, const std::string& iq_file,
                long long audio_samples, long long iq_samples) {
    log_info() << "OPS-SAT PRETTY SDR RF Capture\n";
    log_info() << "Output:      " << cfg.output_wav << "\n";
    log_info() << "I/Q file:    " << iq_file << "\n";
    log_info() << "URI:         " << cfg.uri << "\n";
    log_info() << "Frequency:   " << cfg.frequency / 1e6 << " MHz\n";
    log_info() << "SDR rate:    " << cfg.sdr_rate << " Hz (AD9361 hardware)\n";
    log_info() << "Decimation:  " << cfg.decimation << "x\n";
    log_info() << "Eff. rate:   " << cfg.effective_rate << " Hz (post-LPF)\n";
    log_info() << "RF BW:       " << cfg.rf_bandwidth << " Hz\n";
    log_info() << "Audio rate:  " << cfg.audio_rate << " Hz\n";
    log_info() << "Gain:        " << cfg.gain << " dB\n";
    log_info() << "FM Dev:      " << cfg.fm_deviation << " Hz\n";
    log_info() << "Duration:    " << cfg.duration << " seconds\n";
    log_info() << "LPF cutoff:  " << cfg.lpf_cutoff << " Hz\n";
    log_info() << "LPF trans:   " << cfg.lpf_transition << " Hz\n";
    log_info() << "Bandpass:    " << cfg.bandpass_low << " - " << cfg.bandpass_high << " Hz\n";
    log_info() << "I/Q scale:   " << IQ_SCALE << " (|1.0| -> " << (int)IQ_SCALE << " int16)\n";
    log_info() << "Samples:     " << audio_samples << " audio, " << iq_samples << " I/Q\n";
    log_info() << "Expected .sc16:  " << (double)iq_samples * 4.0 / (1024.0 * 1024.0) << " MiB\n";
}

// Build GNU Radio flowgraph, run capture, then post-process audio.
// interp/decim are the GCD-reduced resampler ratio (effective_rate -> audio_rate).
int run_capture(const CaptureConfig& cfg,
                const std::string& iq_file,
                long long audio_samples, long long iq_samples,
                unsigned long interpolation, unsigned long decimation) {
    if (cfg.effective_rate <= 0 || cfg.audio_rate <= 0) {
        log_error() << "Invalid rates: effective_rate and audio_rate must be > 0\n";
        return 1;
    }

    log_info() << "Resample: " << cfg.effective_rate << " -> " << cfg.audio_rate
               << " (interp=" << interpolation << ", decim=" << decimation << ")\n";

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

    // Design audio bandpass taps
    std::vector<float> bp_taps = gr::filter::firdes::band_pass(
        1.0, cfg.audio_rate, cfg.bandpass_low, cfg.bandpass_high, 200.0,
        gr::fft::window::WIN_HAMMING
    );
    log_info() << "Bandpass taps: " << bp_taps.size() << "\n";

    // --- Configure AD9361 via libiio ---
    // Write all attributes before building the flowgraph. The device_source
    // iio_param_vec_t mechanism does not reliably apply attributes on the
    // flatsat (regression from fmcomms2_source migration). Direct libiio
    // writes match the OPS-SAT SDR experimenter code template approach.
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
        if (!write_iio_rx_config(phy, cfg.frequency, (long long)cfg.sdr_rate,
                                 (long long)cfg.rf_bandwidth, cfg.gain)) {
            iio_context_destroy(cfg_ctx);
            return 1;
        }
        if (cfg.min_readback) {
            log_info() << "Minimal readback mode (--min-readback): sample rate mismatch is non-fatal\n";
        }
        if (!readback_iio_rx_config(phy, cfg.frequency, (long long)cfg.sdr_rate,
                                    (long long)cfg.rf_bandwidth, cfg.gain,
                                    cfg.rate_tolerance, !cfg.min_readback)) {
            iio_context_destroy(cfg_ctx);
            return 1;
        }
        iio_context_destroy(cfg_ctx);
    }

    // --- Build flowgraph ---
    // Retry loop handles stale IIO connections from previously crashed captures.
    // When a capture process is killed by a signal, the IIO server (especially
    // network-based like iio-emu) may take time to detect the dead TCP connection
    // and free the connection slot. Retrying gives it time to recover.
    // On local IIO (real hardware), the first attempt always succeeds.
    gr::top_block_sptr tb;
    gr::blocks::head::sptr iq_head, head;

    const int MAX_BUILD_ATTEMPTS = 3;
    const int BUILD_RETRY_DELAY_SEC = 5;

    for (int attempt = 1; ; attempt++) {
    try {
        log_info() << "Building flowgraph...\n";
        tb = gr::make_top_block("capture_loop");

        // IIO RX source — uses device_source directly instead of
        // fmcomms2_source_fc32 to avoid the overflow-check thread that
        // crashes when FPGA register reads are unsupported (the thread
        // throws std::runtime_error -> std::terminate).
        // AD9361 attributes are written via direct libiio calls in
        // write_iio_rx_config() before the build loop — device_source's
        // iio_param_vec_t mechanism does not reliably apply them.
        gr::iio::iio_param_vec_t no_params;
        std::vector<std::string> iio_channels = {"voltage0", "voltage1"};
        auto iio_src = gr::iio::device_source::make(
            cfg.uri, "cf-ad9361-lpc", iio_channels, "ad9361-phy",
            no_params, 0x8000);

        // Convert device_source shorts to gr_complex.
        // device_source outputs int16 per IIO channel (voltage0=I, voltage1=Q).
        // AD9361 ADC is 12-bit: divide by 2048.0 to normalize to ~+/-1.0
        // (same conversion that fmcomms2_source_fc32::work() performs internally).
        auto i_s2f  = gr::blocks::short_to_float::make(1, 2048.0f);
        auto q_s2f  = gr::blocks::short_to_float::make(1, 2048.0f);
        auto to_fc32 = gr::blocks::float_to_complex::make(1);

        // DSP blocks — LPF decimates from sdr_rate to effective_rate
        auto lpf       = gr::filter::fir_filter_ccf::make(cfg.decimation, lpf_taps);
        auto to_short  = gr::blocks::complex_to_interleaved_short::make(false, IQ_SCALE);
        iq_head        = gr::blocks::head::make(sizeof(gr_complex), iq_samples);
        // iq_samples counts complex samples at effective_rate (post-LPF);
        // sink writes 2 int16 per sample (I,Q) => 4 bytes/sample
        auto iq_sink   = gr::blocks::file_sink::make(sizeof(short), iq_file.c_str(), false);
        // FM demod operates at effective_rate (post-LPF output)
        auto fm_demod  = gr::analog::quadrature_demod_cf::make(
            cfg.effective_rate / (2.0 * M_PI * cfg.fm_deviation)
        );
        // Resample from effective_rate to audio_rate
        auto resampler = gr::filter::rational_resampler_fff::make(interpolation, decimation);
        auto bandpass  = gr::filter::fir_filter_fff::make(1, bp_taps);
        head           = gr::blocks::head::make(sizeof(float), audio_samples);
        auto wav_sink  = gr::blocks::wavfile_sink::make(
            cfg.output_wav.c_str(), 1, cfg.audio_rate,
            gr::blocks::FORMAT_WAV, gr::blocks::FORMAT_PCM_16
        );

        // Connect: IIO src (shorts) -> fc32 conversion -> LPF -> branches
        tb->connect(iio_src, 0, i_s2f, 0);
        tb->connect(iio_src, 1, q_s2f, 0);
        tb->connect(i_s2f, 0, to_fc32, 0);
        tb->connect(q_s2f, 0, to_fc32, 1);
        tb->connect(to_fc32, 0, lpf, 0);

        // Branch 1: LPF -> head -> sc16 conversion -> file_sink (.sc16)
        tb->connect(lpf, 0, iq_head, 0);
        tb->connect(iq_head, 0, to_short, 0);
        tb->connect(to_short, 0, iq_sink, 0);

        // Branch 2: LPF -> FM demod -> resample -> bandpass -> head -> wav_sink
        tb->connect(lpf, 0, fm_demod, 0);
        tb->connect(fm_demod, 0, resampler, 0);
        tb->connect(resampler, 0, bandpass, 0);
        tb->connect(bandpass, 0, head, 0);
        tb->connect(head, 0, wav_sink, 0);

        // --- Start ---
        log_info() << "Starting capture for " << cfg.duration << " seconds...\n";

        tb->start();
        break;  // flowgraph started — exit retry loop
    } catch (const std::exception& e) {
        // Release partial resources (including any IIO contexts held by GNU Radio blocks)
        tb.reset();
        iq_head.reset();
        head.reset();

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
    int timeout_sec = cfg.duration * cfg.timeout_multiplier + 10;
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
            // (head -> complex_to_interleaved_short -> file_sink) before we
            // call tb->stop().  Without this, in-flight buffer items could
            // be lost, producing a slightly short sc16 file.
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            log_info() << "Capture complete (I/Q: " << iq_head->nitems_written(0)
                       << "/" << iq_samples << ", audio: " << head->nitems_written(0)
                       << "/" << audio_samples << " samples, " << elapsed_sec << "s)\n";
            break;
        }

        // Progress every 5 seconds
        if (elapsed_sec > 0 && elapsed_sec % 5 == 0 &&
            std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() % 5000 < 100) {
            log_info() << "Progress [" << elapsed_sec << "s]: I/Q "
                       << iq_head->nitems_written(0) << "/" << iq_samples
                       << ", audio " << head->nitems_written(0) << "/" << audio_samples << "\n";
        }

        if (elapsed >= std::chrono::seconds(timeout_sec)) {
            log_error() << "Timeout after " << elapsed_sec << " seconds"
                        << " (audio: " << head->nitems_written(0) << "/" << audio_samples
                        << ", I/Q: " << iq_head->nitems_written(0) << "/" << iq_samples << ")\n";
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
        log_error() << "Capture may have failed - check SDR connection / IIO buffers\n";
    }

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
    long long actual_iq_written = (long long)iq_head->nitems_written(0);
    long long actual_audio_written = (long long)head->nitems_written(0);

    log_info() << (early_stop ? "Capture stopped early" : "Capture complete") << "\n";
    log_info() << "Wall time: " << total_sec << " seconds\n";
    log_info() << "Audio:     " << cfg.output_wav << "\n";
    log_info() << "I/Q:       " << iq_file << "\n";
    if (early_stop) {
        log_info() << "Partial:   " << actual_iq_written << "/" << iq_samples << " I/Q, "
                   << actual_audio_written << "/" << audio_samples << " audio\n";
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
        log_info() << "sc16 rail hit rate: " << (clip_rate * 100.0) << "% (per int16, +/-32767)\n";
        if (clip_rate > 0.01) {
            log_warning() << "sc16 rail hit rate " << (clip_rate * 100.0)
                        << "% exceeds 1% — RX may be saturating (reduce gain)\n";
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

            if (iq_stats.rms_i < 1.0 && iq_stats.rms_q < 1.0) {
                log_warning() << "IQ signal appears dead (RMS < 1 on both channels)\n";
            }
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
    {
        std::string spec_file = make_spectrogram_filename(iq_file);
        if (!generate_spectrogram(iq_file, spec_file, (long long)cfg.effective_rate)) {
            log_warning() << "spectrogram generation failed\n";
        }
    }

    if (timed_out) return 2;
    if (interrupted) return 128 + g_signal_received;  // conventional: 130 for SIGINT, 143 for SIGTERM
    if (hard_fail) return 1;
    return 0;
}

int main(int argc, char* argv[]) {
    CaptureConfig cfg;

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

    // Cap duration to stay within downlink budget (sc16: 4 bytes per complex sample)
    // File is written at effective_rate (post-LPF decimation)
    const long long max_iq_bytes = (long long)cfg.max_iq_mb * 1024LL * 1024;
    int max_duration = (int)(max_iq_bytes / ((long long)cfg.effective_rate * 4LL));
    log_info() << "Capture cap:  " << max_duration << "s ("
               << cfg.max_iq_mb << " MiB @ "
               << cfg.effective_rate << " SPS sc16)\n";
    if (cfg.duration > max_duration) {
        log_warning() << "duration " << cfg.duration << "s exceeds cap, truncating\n";
        cfg.duration = max_duration;
    }
    if (cfg.duration <= 0) {
        log_error() << "Effective duration must be > 0 (increase max_iq_mb or duration)\n";
        return 1;
    }
    log_info() << "Effective duration: " << cfg.duration << " sec\n";

    // I/Q samples counted at effective_rate (post-LPF output rate)
    long long iq_samples = (long long)cfg.duration * (long long)cfg.effective_rate;
    // Use actual resampler ratio (interp/decim via GCD) for exact integer math.
    // Resampler operates on effective_rate -> audio_rate.
    unsigned long gcd_rates = std::gcd((unsigned long)cfg.effective_rate, (unsigned long)cfg.audio_rate);
    unsigned long interp = cfg.audio_rate / gcd_rates;
    unsigned long decim = cfg.effective_rate / gcd_rates;
    // Sanity cap: absurd ratios produce huge FIR filter tap counts and blow memory
    if (interp > 1000 || decim > 1000) {
        log_error() << "Resampler ratio " << interp << "/" << decim
                    << " too large (effective_rate=" << cfg.effective_rate
                    << ", audio_rate=" << cfg.audio_rate
                    << ") — choose rates with a reasonable GCD\n";
        return 1;
    }
    if (iq_samples % (long long)decim != 0) {
        long long snapped = (iq_samples / (long long)decim) * (long long)decim;
        log_info() << "Snapping iq_samples " << iq_samples << " -> " << snapped
                   << " (multiple of decim=" << decim << ")\n";
        iq_samples = snapped;
        if (iq_samples <= 0) {
            log_error() << "iq_samples snapped to 0 — duration too short for decim\n";
            return 1;
        }
    }
    long long audio_samples = (iq_samples / (long long)decim) * (long long)interp;
    std::string iq_file = make_iq_filename(cfg.output_wav);

    log_config(cfg, iq_file, audio_samples, iq_samples);

    int rc;
    try {
        rc = run_capture(cfg, iq_file, audio_samples, iq_samples, interp, decim);
    } catch (const std::exception& e) {
        log_error() << e.what() << "\n";
        rc = 1;
    }

    return rc;
}
