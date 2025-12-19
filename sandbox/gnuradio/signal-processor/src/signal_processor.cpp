/*
 * GNU Radio Signal Processor for OPS-SAT SEPP
 *
 * Pipeline: WAV Source -> Lowpass -> Bandpass -> Power Squelch -> WAV Sink
 *
 * Optimized for voice audio (300-3400 Hz)
 *
 * Usage: signal_processor [options]
 *   -i, --input       Input WAV file path (required)
 *   -o, --output      Output WAV file path (default: output.wav)
 *   -l, --lowpass     Lowpass cutoff frequency in Hz (default: 3400)
 *   -b, --bandlow     Bandpass low frequency in Hz (default: 300)
 *   -B, --bandhigh    Bandpass high frequency in Hz (default: 3400)
 *   -t, --threshold   Squelch threshold in dB (default: -40)
 *   -h, --help        Show this help message
 */

#include <iostream>
#include <string>
#include <cstring>
#include <csignal>
#include <atomic>

#include <gnuradio/top_block.h>
#include <gnuradio/blocks/wavfile_source.h>
#include <gnuradio/blocks/wavfile_sink.h>
#include <gnuradio/filter/fir_filter_blk.h>
#include <gnuradio/filter/firdes.h>
#include <gnuradio/analog/pwr_squelch_ff.h>

// Global flag for signal handling
std::atomic<bool> g_running(true);

void signal_handler(int signum) {
    std::cout << "Received signal " << signum << ", stopping..." << std::endl;
    g_running = false;
}

void print_usage(const char* program_name) {
    std::cout << "GNU Radio Signal Processor for OPS-SAT SEPP\n"
              << "============================================\n\n"
              << "Pipeline: Lowpass -> Bandpass -> Power Squelch\n"
              << "Optimized for voice audio (300-3400 Hz)\n\n"
              << "Usage: " << program_name << " [options]\n\n"
              << "Options:\n"
              << "  -i, --input       Input WAV file path (required)\n"
              << "  -o, --output      Output WAV file path (default: output.wav)\n"
              << "  -l, --lowpass     Lowpass cutoff frequency in Hz (default: 3400)\n"
              << "  -b, --bandlow     Bandpass low frequency in Hz (default: 300)\n"
              << "  -B, --bandhigh    Bandpass high frequency in Hz (default: 3400)\n"
              << "  -t, --threshold   Squelch threshold in dB (default: -40)\n"
              << "  -h, --help        Show this help message\n";
}

int main(int argc, char* argv[]) {
    // Default parameters - voice frequency range
    std::string input_file;
    std::string output_file = "output.wav";
    double lowpass_freq = 3400.0;
    double bandpass_low = 300.0;
    double bandpass_high = 3400.0;
    double squelch_threshold = -50.0;  // Lower threshold = less aggressive gating

    // Parse command-line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if ((arg == "-i" || arg == "--input") && i + 1 < argc) {
            input_file = argv[++i];
        } else if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            output_file = argv[++i];
        } else if ((arg == "-l" || arg == "--lowpass") && i + 1 < argc) {
            lowpass_freq = std::stod(argv[++i]);
        } else if ((arg == "-b" || arg == "--bandlow") && i + 1 < argc) {
            bandpass_low = std::stod(argv[++i]);
        } else if ((arg == "-B" || arg == "--bandhigh") && i + 1 < argc) {
            bandpass_high = std::stod(argv[++i]);
        } else if ((arg == "-t" || arg == "--threshold") && i + 1 < argc) {
            squelch_threshold = std::stod(argv[++i]);
        } else if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        }
    }

    // Validate required arguments
    if (input_file.empty()) {
        std::cerr << "Error: Input file is required\n\n";
        print_usage(argv[0]);
        return 1;
    }

    std::cout << "GNU Radio Signal Processor - OPS-SAT SEPP\n"
              << "==========================================\n"
              << "Input file: " << input_file << "\n"
              << "Output file: " << output_file << "\n"
              << "Pipeline:\n"
              << "  1. Lowpass: " << lowpass_freq << " Hz\n"
              << "  2. Bandpass: " << bandpass_low << " - " << bandpass_high << " Hz\n"
              << "  3. Power Squelch: " << squelch_threshold << " dB\n";

    // Set up signal handling
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    try {
        // Create top block (flowgraph container)
        gr::top_block_sptr tb = gr::make_top_block("signal_processor");

        // Create WAV file source
        auto source = gr::blocks::wavfile_source::make(
            input_file.c_str(),
            false  // don't repeat
        );

        // Get sample rate from source
        double sample_rate = source->sample_rate();
        int num_channels = source->channels();

        std::cout << "Source sample rate: " << sample_rate << " Hz\n"
                  << "Source channels: " << num_channels << "\n";

        // Stage 1: Design lowpass filter taps
        double lp_transition = lowpass_freq * 0.1;  // 10% of cutoff
        std::vector<float> lowpass_taps = gr::filter::firdes::low_pass(
            1.0,               // gain
            sample_rate,
            lowpass_freq,
            lp_transition
        );
        std::cout << "Lowpass filter taps: " << lowpass_taps.size() << "\n";

        auto lowpass_filter = gr::filter::fir_filter_fff::make(1, lowpass_taps);

        // Stage 2: Design bandpass filter taps
        double bp_transition = bandpass_low * 0.5;  // Transition width
        std::vector<float> bandpass_taps = gr::filter::firdes::band_pass(
            1.0,               // gain
            sample_rate,
            bandpass_low,
            bandpass_high,
            bp_transition
        );
        std::cout << "Bandpass filter taps: " << bandpass_taps.size() << "\n";

        auto bandpass_filter = gr::filter::fir_filter_fff::make(1, bandpass_taps);

        // Stage 3: Power squelch for noise gating
        // Gates the signal when power is below threshold
        auto squelch = gr::analog::pwr_squelch_ff::make(
            squelch_threshold,  // threshold in dB
            0.0001,             // alpha (smoothing factor, slower = smoother)
            1000,               // ramp samples (smooth fade in/out)
            false               // gate mode (false = output zeros when squelched)
        );
        std::cout << "Power squelch threshold: " << squelch_threshold << " dB\n";

        // Create WAV file sink
        auto sink = gr::blocks::wavfile_sink::make(
            output_file.c_str(),
            1,                          // channels (mono output)
            static_cast<int>(sample_rate),
            gr::blocks::FORMAT_WAV,
            gr::blocks::FORMAT_PCM_16
        );

        // Connect the flowgraph:
        // source -> lowpass -> bandpass -> squelch -> sink
        tb->connect(source, 0, lowpass_filter, 0);
        tb->connect(lowpass_filter, 0, bandpass_filter, 0);
        tb->connect(bandpass_filter, 0, squelch, 0);
        tb->connect(squelch, 0, sink, 0);

        std::cout << "Starting flowgraph...\n";

        // Run the flowgraph (blocks until source is exhausted)
        tb->run();

        std::cout << "Flowgraph completed successfully.\n"
                  << "Output written to: " << output_file << "\n";

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
