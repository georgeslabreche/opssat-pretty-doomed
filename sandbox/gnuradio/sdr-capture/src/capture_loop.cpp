/*
 * capture_loop.cpp - GNU Radio IIO RF Capture for OPS-SAT
 *
 * Continuous RX capture loop for live RF reception.
 * Captures FM-demodulated audio from AD9361 SDR.
 *
 * Usage: capture_loop -o output.wav -d duration_seconds
 */

#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <cmath>
#include <csignal>

#include <gnuradio/top_block.h>
#include <gnuradio/blocks/wavfile_sink.h>
#include <gnuradio/blocks/file_sink.h>
#include <gnuradio/blocks/head.h>
#include <gnuradio/analog/quadrature_demod_cf.h>
#include <gnuradio/filter/rational_resampler.h>
#include <gnuradio/iio/fmcomms2_source.h>
#include <numeric>  // for std::gcd

// Global flags for signal handling (only volatile sig_atomic_t is async-signal-safe)
volatile sig_atomic_t g_running = 1;
volatile sig_atomic_t g_signal_received = 0;

void signal_handler(int signum) {
    g_signal_received = signum;
    g_running = 0;
}

void print_usage(const char* prog) {
    std::cout << "OPS-SAT SDR RF Capture\n"
              << "======================\n\n"
              << "Usage: " << prog << " [options]\n\n"
              << "Options:\n"
              << "  -o, --output    Output WAV file (default: capture.wav)\n"
              << "  -d, --duration  Capture duration in seconds (default: 10)\n"
              << "  -u, --uri       IIO URI (default: local:)\n"
              << "  -f, --freq      Frequency in Hz (default: 1296000000)\n"
              << "  -s, --rate      Sample rate (default: 528000)\n"
              << "  -g, --gain      RX gain in dB (default: 50)\n"
              << "  -e, --deviation FM deviation in Hz (default: 5000)\n"
              << "  -h, --help      Show this help\n";
}

int main(int argc, char* argv[]) {
    // Default settings
    std::string output_wav = "capture.wav";
    std::string uri = "local:";
    long long frequency = 1296000000;  // 1296 MHz (23cm amateur band)
    long sample_rate = 528000;
    double fm_deviation = 5000.0;
    double gain = 50.0;
    int duration = 10;  // seconds
    int audio_rate = 48000;

    // Parse arguments
    try {
        for (int i = 1; i < argc; i++) {
            std::string arg = argv[i];
            if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
                output_wav = argv[++i];
            } else if ((arg == "-d" || arg == "--duration") && i + 1 < argc) {
                duration = std::stoi(argv[++i]);
            } else if ((arg == "-u" || arg == "--uri") && i + 1 < argc) {
                uri = argv[++i];
            } else if ((arg == "-f" || arg == "--freq") && i + 1 < argc) {
                frequency = std::stoll(argv[++i]);
            } else if ((arg == "-s" || arg == "--rate") && i + 1 < argc) {
                sample_rate = std::stol(argv[++i]);
            } else if ((arg == "-g" || arg == "--gain") && i + 1 < argc) {
                gain = std::stod(argv[++i]);
            } else if ((arg == "-e" || arg == "--deviation") && i + 1 < argc) {
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

    // Validate inputs
    if (duration <= 0) {
        std::cerr << "Error: Duration must be > 0\n";
        return 1;
    }
    if (sample_rate <= 0) {
        std::cerr << "Error: Sample rate must be > 0\n";
        return 1;
    }

    // Setup signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Calculate samples to capture
    long long samples_to_capture = (long long)duration * audio_rate;
    // Calculate I/Q samples at SDR sample rate
    long long iq_samples = (long long)duration * sample_rate;

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

    std::cout << "OPS-SAT SDR RF Capture\n"
              << "======================\n"
              << "Output:    " << output_wav << "\n"
              << "I/Q file:  " << iq_file << "\n"
              << "URI:       " << uri << "\n"
              << "Frequency: " << frequency / 1e6 << " MHz\n"
              << "Rate:      " << sample_rate << " Hz\n"
              << "Gain:      " << gain << " dB\n"
              << "FM Dev:    " << fm_deviation << " Hz\n"
              << "Duration:  " << duration << " seconds\n"
              << "Samples:   " << samples_to_capture << " audio, " << iq_samples << " I/Q\n\n";

    try {
        // Calculate resampling ratio using GCD for accurate conversion
        unsigned long gcd_val = std::gcd((unsigned long)sample_rate, (unsigned long)audio_rate);
        unsigned long interpolation = audio_rate / gcd_val;
        unsigned long decimation = sample_rate / gcd_val;
        std::cout << "Resample: " << sample_rate << " -> " << audio_rate
                  << " (interp=" << interpolation << ", decim=" << decimation << ")\n\n";

        // Build flowgraph
        std::cout << "Building flowgraph...\n";
        gr::top_block_sptr tb = gr::make_top_block("capture_loop");

        // Channel enable: RX1 only
        std::vector<bool> rx_ch_en = {true, false};

        // IIO RX source using fmcomms2_source
        auto iio_src = gr::iio::fmcomms2_source_fc32::make(
            uri,
            rx_ch_en,
            0x8000     // Buffer size
        );
        iio_src->set_frequency(frequency);
        iio_src->set_samplerate(sample_rate);
        iio_src->set_gain_mode(0, "manual");
        iio_src->set_gain(0, gain);
        iio_src->set_quadrature(true);
        iio_src->set_rfdc(true);
        iio_src->set_bbdc(true);
        iio_src->set_filter_params("auto", "", 0, 0);

        // FM demodulator
        auto fm_demod = gr::analog::quadrature_demod_cf::make(
            sample_rate / (2.0 * M_PI * fm_deviation)
        );

        // Resample to audio rate
        auto resampler = gr::filter::rational_resampler_fff::make(interpolation, decimation);

        // Limit samples (for fixed duration)
        auto head = gr::blocks::head::make(sizeof(float), samples_to_capture);

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

        // Connect RX paths
        // Branch 1: I/Q file output (raw complex samples)
        tb->connect(iio_src, 0, iq_head, 0);
        tb->connect(iq_head, 0, iq_sink, 0);
        // Branch 2: FM demod -> resample -> audio WAV output
        tb->connect(iio_src, 0, fm_demod, 0);
        tb->connect(fm_demod, 0, resampler, 0);
        tb->connect(resampler, 0, head, 0);
        tb->connect(head, 0, wav_sink, 0);

        // Run
        std::cout << "Starting capture for " << duration << " seconds...\n";
        auto start_time = std::chrono::steady_clock::now();

        tb->start();

        // Wait for completion or interrupt
        while (g_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            auto elapsed = std::chrono::steady_clock::now() - start_time;
            auto elapsed_sec = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();

            if (elapsed_sec >= duration) {
                break;
            }
        }

        // Print signal message outside handler (async-signal-safe)
        if (g_signal_received != 0) {
            std::cout << "\nReceived signal " << g_signal_received << ", stopping...\n";
        }

        tb->stop();
        tb->wait();

        auto end_time = std::chrono::steady_clock::now();
        auto total_sec = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time).count();

        std::cout << "\nCapture complete!\n"
                  << "Duration: " << total_sec << " seconds\n"
                  << "Audio:    " << output_wav << "\n"
                  << "I/Q:      " << iq_file << "\n";

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
