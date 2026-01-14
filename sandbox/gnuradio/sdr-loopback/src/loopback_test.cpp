/*
 * loopback_test.cpp - GNU Radio IIO Loopback Test for OPS-SAT
 *
 * Validates AD9361 SDR integration using internal loopback mode.
 * TX: WAV file -> FM modulate -> AD9361 TX
 * RX: AD9361 RX -> FM demodulate -> WAV file
 *
 * Loopback mode routes TX internally to RX (no RF emission).
 *
 * Usage: loopback_test -i input.wav -o output.wav
 */

#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <cmath>
#include <csignal>

#include <gnuradio/top_block.h>
#include <gnuradio/blocks/wavfile_source.h>
#include <gnuradio/blocks/wavfile_sink.h>
#include <gnuradio/blocks/file_sink.h>
#include <gnuradio/blocks/multiply_const.h>
#include <gnuradio/blocks/head.h>
#include <gnuradio/analog/frequency_modulator_fc.h>
#include <gnuradio/analog/quadrature_demod_cf.h>
#include <gnuradio/filter/rational_resampler.h>
#include <gnuradio/iio/fmcomms2_source.h>
#include <gnuradio/iio/fmcomms2_sink.h>
#include <numeric>  // for std::gcd

#include <iio.h>
#include <sndfile.h>  // For getting WAV file sample count

// Global flags for signal handling (only volatile sig_atomic_t is async-signal-safe)
volatile sig_atomic_t g_running = 1;
volatile sig_atomic_t g_signal_received = 0;

void signal_handler(int signum) {
    g_signal_received = signum;
    g_running = 0;
}

void print_usage(const char* prog) {
    std::cout << "OPS-SAT IIO Loopback Test\n"
              << "=========================\n\n"
              << "Usage: " << prog << " [options]\n\n"
              << "Options:\n"
              << "  -i, --input     Input WAV file (required)\n"
              << "  -o, --output    Output WAV file (default: output.wav)\n"
              << "  -u, --uri       IIO URI (default: local:)\n"
              << "  -f, --freq      Frequency in Hz (default: 1296000000)\n"
              << "  -s, --rate      Sample rate (default: 528000)\n"
              << "  -d, --deviation FM deviation in Hz (default: 5000)\n"
              << "  -h, --help      Show this help\n";
}

int main(int argc, char* argv[]) {
    // Default settings
    std::string input_wav;
    std::string output_wav = "output.wav";
    std::string uri = "local:";
    unsigned long long frequency = 1296000000;  // 1296 MHz (23cm amateur band)
    unsigned long sample_rate = 528000;
    unsigned long bandwidth = 200000;
    double fm_deviation = 5000.0;
    double tx_attenuation = 10.0;  // dB
    double rx_gain = 50.0;         // dB

    // Parse arguments
    try {
        for (int i = 1; i < argc; i++) {
            std::string arg = argv[i];
            if ((arg == "-i" || arg == "--input") && i + 1 < argc) {
                input_wav = argv[++i];
            } else if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
                output_wav = argv[++i];
            } else if ((arg == "-u" || arg == "--uri") && i + 1 < argc) {
                uri = argv[++i];
            } else if ((arg == "-f" || arg == "--freq") && i + 1 < argc) {
                frequency = std::stoull(argv[++i]);
            } else if ((arg == "-s" || arg == "--rate") && i + 1 < argc) {
                sample_rate = std::stoul(argv[++i]);
                if (sample_rate == 0) {
                    std::cerr << "Error: Sample rate must be > 0\n";
                    return 1;
                }
            } else if ((arg == "-d" || arg == "--deviation") && i + 1 < argc) {
                fm_deviation = std::stod(argv[++i]);
                if (fm_deviation <= 0) {
                    std::cerr << "Error: FM deviation must be > 0\n";
                    return 1;
                }
            } else if (arg == "-h" || arg == "--help") {
                print_usage(argv[0]);
                return 0;
            } else if (arg[0] == '-') {
                std::cerr << "Error: Unknown option: " << arg << "\n\n";
                print_usage(argv[0]);
                return 1;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: Invalid argument value: " << e.what() << "\n\n";
        print_usage(argv[0]);
        return 1;
    }

    if (input_wav.empty()) {
        std::cerr << "Error: Input WAV file required\n\n";
        print_usage(argv[0]);
        return 1;
    }

    // Setup signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    std::cout << "OPS-SAT IIO Loopback Test\n"
              << "=========================\n"
              << "Input:     " << input_wav << "\n"
              << "Output:    " << output_wav << "\n"
              << "URI:       " << uri << "\n"
              << "Frequency: " << frequency / 1e6 << " MHz\n"
              << "Rate:      " << sample_rate << " Hz\n"
              << "FM Dev:    " << fm_deviation << " Hz\n\n";

    // --- Enable Loopback Mode via libiio ---
    std::cout << "Connecting to IIO context...\n";
    struct iio_context* ctx = iio_create_context_from_uri(uri.c_str());
    if (!ctx) {
        std::cerr << "Error: Could not connect to IIO context at " << uri << "\n";
        return 1;
    }

    struct iio_device* phy = iio_context_find_device(ctx, "ad9361-phy");
    if (!phy) {
        std::cerr << "Error: Could not find ad9361-phy device\n";
        iio_context_destroy(ctx);
        return 1;
    }

    std::cout << "Enabling loopback mode...\n";
    int ret = iio_device_attr_write(phy, "loopback", "1");
    if (ret < 0) {
        std::cerr << "Warning: Could not enable loopback (ret=" << ret << ")\n";
        std::cerr << "Continuing anyway - may work on some configurations\n";
    } else {
        std::cout << "Loopback mode enabled\n";
    }

    try {
        // Get audio sample rate and sample count from input file using libsndfile
        SF_INFO sf_info = {0};  // Zero-initialize all fields (format=0 required for read)
        SNDFILE* sf = sf_open(input_wav.c_str(), SFM_READ, &sf_info);
        if (!sf) {
            std::cerr << "Error: Could not open WAV file: " << input_wav << "\n";
            std::cerr << "Reason: " << sf_strerror(NULL) << "\n";
            iio_device_attr_write(phy, "loopback", "0");
            iio_context_destroy(ctx);
            return 1;
        }
        int audio_rate = sf_info.samplerate;
        int channels = sf_info.channels;
        long long input_samples = sf_info.frames;
        sf_close(sf);

        // Warn about stereo/multi-channel files
        if (channels > 1) {
            std::cerr << "Warning: Input file has " << channels << " channels, using only channel 1 (left)\n";
        }

        // Validate WAV file properties
        if (audio_rate <= 0) {
            std::cerr << "Error: Invalid WAV file sample rate: " << audio_rate << "\n";
            iio_device_attr_write(phy, "loopback", "0");
            iio_context_destroy(ctx);
            return 1;
        }
        if (input_samples <= 0) {
            std::cerr << "Error: WAV file is empty or invalid\n";
            iio_device_attr_write(phy, "loopback", "0");
            iio_context_destroy(ctx);
            return 1;
        }

        double duration_sec = (double)input_samples / audio_rate;
        std::cout << "Audio rate:   " << audio_rate << " Hz\n";
        std::cout << "Input frames: " << input_samples << " (" << duration_sec << " sec)\n";

        // Calculate resampling factors using GCD for accurate conversion
        // upsample_factor: multiply to go from audio rate to SDR rate
        // downsample_factor: multiply to go from SDR rate to audio rate
        unsigned long gcd_val = std::gcd((unsigned long)sample_rate, (unsigned long)audio_rate);
        unsigned long upsample_factor = sample_rate / gcd_val;    // For audio → SDR
        unsigned long downsample_factor = audio_rate / gcd_val;   // For SDR → audio
        std::cout << "Resample:     " << audio_rate << " -> " << sample_rate
                  << " (up=" << upsample_factor << ", down=" << downsample_factor << ")\n";

        // Calculate expected RX samples (add 10% margin for latency using integer math)
        long long rx_samples = input_samples + input_samples / 10;
        // Calculate I/Q samples at SDR sample rate
        long long iq_samples = (rx_samples * sample_rate) / audio_rate;
        std::cout << "RX samples:   " << rx_samples << " audio, " << iq_samples << " I/Q (with 10% margin)\n\n";

        // Generate I/Q filename from output WAV filename
        std::string iq_file = output_wav;
        size_t dot_pos = iq_file.rfind('.');
        size_t sep_pos = iq_file.find_last_of("/\\");
        // Only treat dot as extension if it's after path separator and not at start of filename
        bool has_extension = (dot_pos != std::string::npos) &&
                             (sep_pos == std::string::npos || dot_pos > sep_pos + 1);
        if (has_extension) {
            iq_file = iq_file.substr(0, dot_pos) + ".cf32";
        } else {
            iq_file += ".cf32";
        }
        std::cout << "I/Q file:     " << iq_file << "\n\n";

        // --- Build Flowgraph ---
        std::cout << "Building flowgraph...\n";
        gr::top_block_sptr tb = gr::make_top_block("loopback_test");

        // TX Path: WAV -> Resample -> FM Mod -> AD9361 TX
        auto wav_src = gr::blocks::wavfile_source::make(input_wav.c_str(), false);

        // Resample audio to SDR sample rate (upsample)
        auto resampler_tx = gr::filter::rational_resampler_fff::make(upsample_factor, downsample_factor);

        // Scale audio for FM modulator
        auto scaler = gr::blocks::multiply_const_ff::make(0.8);

        // FM modulator
        double sensitivity = 2.0 * M_PI * fm_deviation / sample_rate;
        auto fm_mod = gr::analog::frequency_modulator_fc::make(sensitivity);

        // Channel enable: TX1 only
        std::vector<bool> tx_ch_en = {true, false};
        std::vector<bool> rx_ch_en = {true, false};

        // IIO TX sink using fmcomms2_sink
        auto iio_sink = gr::iio::fmcomms2_sink_fc32::make(
            uri,
            tx_ch_en,
            0x8000,    // Buffer size
            false      // Not cyclic
        );
        iio_sink->set_frequency(frequency);
        iio_sink->set_samplerate(sample_rate);
        iio_sink->set_bandwidth(bandwidth);
        iio_sink->set_attenuation(0, tx_attenuation);
        iio_sink->set_filter_params("auto", "", 0, 0);

        // IIO RX source using fmcomms2_source
        auto iio_src = gr::iio::fmcomms2_source_fc32::make(
            uri,
            rx_ch_en,
            0x8000     // Buffer size
        );
        iio_src->set_frequency(frequency);
        iio_src->set_samplerate(sample_rate);
        iio_src->set_gain_mode(0, "manual");  // Use manual mode since we set gain explicitly
        iio_src->set_gain(0, rx_gain);
        iio_src->set_quadrature(true);
        iio_src->set_rfdc(true);
        iio_src->set_bbdc(true);
        iio_src->set_filter_params("auto", "", 0, 0);

        // FM demodulator
        auto fm_demod = gr::analog::quadrature_demod_cf::make(
            sample_rate / (2.0 * M_PI * fm_deviation)
        );

        // Resample back to audio rate (downsample)
        auto resampler_rx = gr::filter::rational_resampler_fff::make(downsample_factor, upsample_factor);

        // Limit RX samples to match input duration (plus margin)
        auto rx_head = gr::blocks::head::make(sizeof(float), rx_samples);

        // WAV output
        auto wav_sink = gr::blocks::wavfile_sink::make(
            output_wav.c_str(),
            1,                   // Mono
            audio_rate,
            gr::blocks::FORMAT_WAV,
            gr::blocks::FORMAT_PCM_16
        );

        // I/Q file output (complex float32, 8 bytes per sample)
        auto iq_head = gr::blocks::head::make(sizeof(gr_complex), iq_samples);
        auto iq_sink = gr::blocks::file_sink::make(sizeof(gr_complex), iq_file.c_str(), false);

        // Connect TX path
        tb->connect(wav_src, 0, resampler_tx, 0);
        tb->connect(resampler_tx, 0, scaler, 0);
        tb->connect(scaler, 0, fm_mod, 0);
        tb->connect(fm_mod, 0, iio_sink, 0);

        // Connect RX path (with head block to limit samples)
        // Branch 1: I/Q file output (raw complex samples)
        tb->connect(iio_src, 0, iq_head, 0);
        tb->connect(iq_head, 0, iq_sink, 0);
        // Branch 2: FM demod -> audio WAV output
        tb->connect(iio_src, 0, fm_demod, 0);
        tb->connect(fm_demod, 0, resampler_rx, 0);
        tb->connect(resampler_rx, 0, rx_head, 0);
        tb->connect(rx_head, 0, wav_sink, 0);

        // --- Run ---
        std::cout << "Starting flowgraph...\n";
        auto start_time = std::chrono::steady_clock::now();
        // Timeout: 2x expected duration + 10 seconds buffer
        int timeout_sec = (int)(duration_sec * 2) + 10;
        tb->start();

        // Wait for RX head block to complete (or user interrupt or timeout)
        bool timed_out = false;
        while (g_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            // Check if flowgraph has naturally completed
            if (rx_head->nitems_written(0) >= (uint64_t)rx_samples) {
                std::cout << "\nCapture complete (received " << rx_samples << " samples)\n";
                break;
            }
            // Check for timeout
            auto elapsed = std::chrono::steady_clock::now() - start_time;
            auto elapsed_sec = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
            if (elapsed_sec >= timeout_sec) {
                std::cerr << "\nWarning: Timeout after " << elapsed_sec << " seconds "
                          << "(received " << rx_head->nitems_written(0) << "/" << rx_samples << " samples)\n";
                timed_out = true;
                break;
            }
        }

        // Print signal message outside handler (async-signal-safe)
        if (g_signal_received != 0) {
            std::cout << "\nReceived signal " << g_signal_received << ", stopping...\n";
        }
        if (timed_out) {
            std::cerr << "Loopback may have failed - check SDR connection\n";
        }

        std::cout << "Stopping flowgraph...\n";
        tb->stop();
        tb->wait();

        std::cout << "\nLoopback test complete!\n"
                  << "Audio: " << output_wav << "\n"
                  << "I/Q:   " << iq_file << "\n";

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        iio_device_attr_write(phy, "loopback", "0");
        iio_context_destroy(ctx);
        return 1;
    }

    // Disable loopback
    std::cout << "Disabling loopback...\n";
    iio_device_attr_write(phy, "loopback", "0");
    iio_context_destroy(ctx);

    return 0;
}
