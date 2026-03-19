/**
 * PRETTY DOOMed - Voice-command-to-DOOM pipeline for OPS-SAT
 *
 * Pipeline: WAV -> Lowpass -> Bandpass -> Resample -> STT -> Match -> DOOM
 */

#include <iostream>
#include <fstream>
#include <string>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <getopt.h>

#include <csignal>

#include "pretty_log.h"
#include "pretty_signal.h"

#include "config.h"
#include "audio_io.h"
#include "dsp.h"
#include "transcriber.h"
#include "matcher.h"
#include "output.h"
#include "executor.h"
#include "sdr_capture.h"

using namespace pretty;

static std::string format_duration(std::chrono::steady_clock::duration d) {
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(d).count();
    return std::to_string(ms / 1000) + "." + std::to_string((ms % 1000) / 100) + "s";
}

static void compute_audio_stats(const std::vector<float>& samples,
                                float& rms, float& peak) {
    double sum_sq = 0.0;
    peak = 0.0f;
    for (float s : samples) {
        float a = std::fabs(s);
        sum_sq += static_cast<double>(s) * s;
        if (a > peak) peak = a;
    }
    rms = samples.empty() ? 0.0f : static_cast<float>(std::sqrt(sum_sq / samples.size()));
}

struct Args {
    std::string input_file;
    std::string config_file;
    std::string variants_file;
    std::string output_dir;
    std::string demos_dir;
    std::string doom_binary;
    bool verbose = false;
    bool sdr_capture = false;
};

void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " [options]\n"
              << "\nRequired:\n"
              << "  -i <file>   Input WAV file (not needed with -s)\n"
              << "  -c <file>   Pipeline config file\n"
              << "  -f <file>   Fuzzy match variants file\n"
              << "  -o <dir>    Output directory\n"
              << "  -d <dir>    DOOM demo files directory\n"
              << "  -e <file>   DOOM binary path\n"
              << "\nOptional:\n"
              << "  -s          SDR capture mode (capture RF audio from AD9361)\n"
              << "  -v          Verbose output\n"
              << "  -h          Show this help\n";
}

bool parse_args(int argc, char** argv, Args& args) {
    int opt;
    while ((opt = getopt(argc, argv, "i:c:f:o:d:e:svh")) != -1) {
        switch (opt) {
            case 'i': args.input_file = optarg; break;
            case 'c': args.config_file = optarg; break;
            case 'f': args.variants_file = optarg; break;
            case 'o': args.output_dir = optarg; break;
            case 'd': args.demos_dir = optarg; break;
            case 'e': args.doom_binary = optarg; break;
            case 's': args.sdr_capture = true; break;
            case 'v': args.verbose = true; break;
            case 'h': print_usage(argv[0]); return false;
            default: print_usage(argv[0]); return false;
        }
    }

    if (!args.sdr_capture && args.input_file.empty()) {
        std::cerr << "Error: Input WAV file required (use -i or -s for SDR capture)\n\n";
        print_usage(argv[0]);
        return false;
    }
    if (args.config_file.empty() || args.variants_file.empty() ||
        args.output_dir.empty() || args.demos_dir.empty() ||
        args.doom_binary.empty()) {
        std::cerr << "Error: All required options must be specified\n\n";
        print_usage(argv[0]);
        return false;
    }

    return true;
}

static void write_file(const std::string& path, const std::string& content) {
    std::ofstream out(path);
    if (out) {
        out << content;
    } else {
        log_warning() << "Cannot write to " << path << "\n";
    }
}

int main(int argc, char** argv) {
    Args args;
    if (!parse_args(argc, argv, args)) {
        return 1;
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    log_info() << "=== PRETTY DOOMed ===\n";
    auto pipeline_start = std::chrono::steady_clock::now();

    // Step 1: Load config
    log_info() << "Loading config: " << args.config_file << "\n";
    PipelineConfig cfg;
    if (!load_config(args.config_file, cfg)) {
        log_error() << "Cannot load config: " << args.config_file << "\n";
        return 1;
    }

    VariantsMap variants;
    if (!load_variants(args.variants_file, variants)) {
        log_error() << "Cannot load variants: " << args.variants_file << "\n";
        return 1;
    }

    if (args.verbose) {
        log_info() << "  Wake word: " << cfg.wake_word << "\n";
        log_info() << "  Call signs: " << cfg.call_signs.size() << " configured\n";
        std::string cmds_str = "  Commands: ";
        for (size_t i = 0; i < cfg.commands.size(); i++) {
            if (i > 0) cmds_str += ", ";
            cmds_str += cfg.commands[i];
        }
        log_info() << cmds_str << "\n";
        log_info() << "  Decoding: " << cfg.decoding_method << "\n";
        log_info() << "  Fuzzy distance: " << cfg.fuzzy_max_distance << "\n";
        log_info() << "  Variants: " << variants.size() << " entries\n";
    }

    // Step 1.5: SDR Capture (if --sdr-capture mode)
    if (args.sdr_capture) {
        log_info() << "SDR Capture mode\n";
        SdrCaptureResult sdr_result;
        if (!run_sdr_capture(cfg, args.output_dir, sdr_result)) {
            log_error() << "SDR capture failed\n";
            return 1;
        }
        args.input_file = sdr_result.wav_path;
        log_info() << "SDR capture output: " << args.input_file << "\n";
    }

    // Step 2: Read audio
    log_info() << "Reading audio: " << args.input_file << "\n";
    std::vector<float> samples;
    int sample_rate;
    if (!read_wav(args.input_file, samples, sample_rate)) {
        return 1;
    }
    log_info() << "  " << sample_rate << " Hz, " << samples.size()
               << " samples (" << static_cast<float>(samples.size()) / sample_rate << "s)\n";

    AudioStats audio_stats;
    compute_audio_stats(samples, audio_stats.rms_raw, audio_stats.peak_raw);

    // Step 3: GNU Radio signal processing
    log_info() << "Filtering (GNU Radio)...\n";
    if (args.verbose) {
        log_info() << "  Lowpass: " << cfg.lowpass_cutoff << " Hz\n";
        log_info() << "  Bandpass: " << cfg.bandpass_low << "-" << cfg.bandpass_high << " Hz\n";
    }

    auto dsp_start = std::chrono::steady_clock::now();
    auto filtered = apply_lowpass(samples, sample_rate,
                                   cfg.lowpass_cutoff, cfg.lowpass_transition);
    filtered = apply_bandpass(filtered, sample_rate,
                               cfg.bandpass_low, cfg.bandpass_high,
                               cfg.bandpass_transition);

    compute_audio_stats(filtered, audio_stats.rms_filtered, audio_stats.peak_filtered);

    // Write denoised audio
    std::string denoised_path = args.output_dir + "/processed.wav";
    write_wav(denoised_path, filtered, sample_rate);
    log_info() << "  Denoised audio: " << denoised_path << "\n";

    // Step 4: Resample to 16 kHz
    log_info() << "Resampling to 16 kHz...\n";
    auto resampled = resample(filtered, sample_rate, 16000);
    auto dsp_end = std::chrono::steady_clock::now();
    log_info() << "  " << resampled.size() << " samples\n";
    log_info() << "  DSP time: " << format_duration(dsp_end - dsp_start) << "\n";

    // Step 5: Transcribe
    log_info() << "Transcribing (" << cfg.decoding_method << ")...\n";
    auto stt_start = std::chrono::steady_clock::now();
    std::string transcript = transcribe(resampled, 16000, cfg);
    auto stt_end = std::chrono::steady_clock::now();
    log_info() << "  STT time: " << format_duration(stt_end - stt_start) << "\n";

    if (transcript.empty()) {
        log_error() << "Transcription produced no output\n";
        // Still write empty files for diagnostics
        write_file(args.output_dir + "/transcription.txt", "");
        write_file(args.output_dir + "/summary.txt", "Transcription failed.\n");
        return 1;
    }

    log_info() << "  Transcription: " << transcript << "\n";
    write_file(args.output_dir + "/transcription.txt", transcript + "\n");

    // Step 6: Detect command
    log_info() << "Detecting command...\n";
    DetectionResult detection = detect(transcript, cfg, variants);

    int wake_total = detection.wake_word_exact + detection.wake_word_approx;
    log_info() << "  Wake word: " << wake_total
               << " (exact=" << detection.wake_word_exact
               << ", approx=" << detection.wake_word_approx << ")\n";
    for (const auto& [cmd, ec] : detection.command_exact_counts) {
        int ac = 0;
        auto it = detection.command_approx_counts.find(cmd);
        if (it != detection.command_approx_counts.end()) ac = it->second;
        log_info() << "  Command [" << cmd << "]: " << (ec + ac)
                   << " (exact=" << ec << ", approx=" << ac << ")\n";
    }
    for (const auto& [cmd, ac] : detection.command_approx_counts) {
        if (detection.command_exact_counts.count(cmd) == 0) {
            log_info() << "  Command [" << cmd << "]: " << ac
                       << " (exact=0, approx=" << ac << ")\n";
        }
    }
    for (const auto& [sign, count] : detection.call_sign_counts) {
        log_info() << "  Call sign [" << sign << "]: " << count << "\n";
    }

    // Read ASCII art for summary (optional, from ascii.txt next to binary or config)
    std::string ascii_art;
    {
        // Try directory of config file first, then current directory
        std::string cfg_dir = args.config_file;
        auto pos = cfg_dir.find_last_of('/');
        std::string ascii_path = (pos != std::string::npos)
            ? cfg_dir.substr(0, pos + 1) + "ascii.txt"
            : "ascii.txt";
        std::ifstream af(ascii_path);
        if (af) {
            ascii_art.assign(std::istreambuf_iterator<char>(af),
                             std::istreambuf_iterator<char>());
        }
    }

    // Write scores + summary
    DetectionTotals totals = compute_totals(detection);
    write_file(args.output_dir + "/scores.txt", format_scores(detection, totals, audio_stats));
    write_file(args.output_dir + "/summary.txt",
               format_summary(detection, totals, cfg, args.input_file, transcript, ascii_art));

    // Step 7: Launch DOOM if command detected
    if (totals.command_detected) {
        log_info() << "Command detected! Launching DOOM...\n";
        if (!ascii_art.empty()) {
            std::cout << ascii_art << std::endl;
        }
        int result = run_doom(args.doom_binary, args.demos_dir, args.output_dir,
                              cfg.doom_frames, cfg.doom_maxframes,
                              cfg.doom_keepgifframes);
        if (result != 0) {
            log_warning() << "DOOM had " << result << " failure(s)\n";
        }
    } else {
        log_info() << "No command detected.\n";
    }

    auto pipeline_end = std::chrono::steady_clock::now();
    log_info() << "Total time: " << format_duration(pipeline_end - pipeline_start) << "\n";
    log_info() << "=== Done ===\n";
    return totals.command_detected ? 0 : 2;
}
