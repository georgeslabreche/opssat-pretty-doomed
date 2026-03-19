/**
 * PRETTY DOOMed - Voice-command-to-DOOM pipeline for OPS-SAT
 *
 * Pipeline: WAV -> Lowpass -> Bandpass -> Resample -> STT -> Match -> DOOM
 *
 * Modes:
 *   -i <file>  Process a single WAV file
 *   -s         SDR capture mode: N sequential captures, then process each
 */

#include <iostream>
#include <fstream>
#include <string>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <thread>
#include <vector>
#include <getopt.h>

#include <csignal>
#include <cstdio>
#include <unistd.h>

#include "pretty_log.h"
#include "pretty_signal.h"

#include "config.h"
#include "audio_io.h"
#include "dsp.h"
#include "transcriber.h"
#include "matcher.h"
#include "output.h"
#include "executor.h"
#include "capture.h"

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

// Switch stdout+stderr to a new log file. Returns the saved fd to restore later.
static int switch_log(const std::string& path) {
    int saved = dup(fileno(stdout));
    std::fflush(stdout);
    std::fflush(stderr);
    FILE* fp = freopen(path.c_str(), "w", stdout);
    if (fp) {
        dup2(fileno(stdout), fileno(stderr));
    }
    return saved;
}

// Restore stdout+stderr from a saved fd.
static void restore_log(int saved_fd) {
    std::fflush(stdout);
    std::fflush(stderr);
    dup2(saved_fd, fileno(stdout));
    dup2(saved_fd, fileno(stderr));
    close(saved_fd);
}

static void write_file(const std::string& path, const std::string& content) {
    std::ofstream out(path);
    if (out) {
        out << content;
    } else {
        log_warning() << "Cannot write to " << path << "\n";
    }
}

// Process a single WAV file through the pipeline: DSP -> STT -> detect -> DOOM.
// Returns true if a command was detected.
static bool process_wav(const std::string& input_file,
                        const std::string& output_dir,
                        const PipelineConfig& cfg,
                        const VariantsMap& variants,
                        const Args& args,
                        Transcriber& stt) {
    log_info() << "--- Processing: " << input_file << " ---\n";
    auto proc_start = std::chrono::steady_clock::now();

    // Read audio
    log_info() << "Reading audio: " << input_file << "\n";
    std::vector<float> samples;
    int sample_rate;
    if (!read_wav(input_file, samples, sample_rate)) {
        return false;
    }
    log_info() << "  " << sample_rate << " Hz, " << samples.size()
               << " samples (" << static_cast<float>(samples.size()) / sample_rate << "s)\n";

    AudioStats audio_stats;
    compute_audio_stats(samples, audio_stats.rms_raw, audio_stats.peak_raw);

    // DSP: lowpass -> bandpass -> resample
    log_info() << "Filtering (GNU Radio)...\n";
    auto dsp_start = std::chrono::steady_clock::now();
    auto filtered = apply_lowpass(samples, sample_rate,
                                   cfg.lowpass_cutoff, cfg.lowpass_transition);
    filtered = apply_bandpass(filtered, sample_rate,
                               cfg.bandpass_low, cfg.bandpass_high,
                               cfg.bandpass_transition);

    compute_audio_stats(filtered, audio_stats.rms_filtered, audio_stats.peak_filtered);

    std::string denoised_path = output_dir + "/processed.wav";
    write_wav(denoised_path, filtered, sample_rate);

    log_info() << "Resampling to 16 kHz...\n";
    auto resampled = resample(filtered, sample_rate, 16000);
    auto dsp_end = std::chrono::steady_clock::now();
    log_info() << "  " << resampled.size() << " samples, DSP time: "
               << format_duration(dsp_end - dsp_start) << "\n";

    // STT (minimum ~0.5s of audio needed for the model's Conv layers)
    if (resampled.size() < 8000) {
        log_warning() << "Audio too short for STT (" << resampled.size()
                      << " samples, need at least 8000). Skipping transcription.\n";
        write_file(output_dir + "/transcription.txt", "");
        write_file(output_dir + "/summary.txt", "Audio too short for transcription.\n");
        return false;
    }

    log_info() << "Transcribing (" << cfg.decoding_method << ")...\n";
    auto stt_start = std::chrono::steady_clock::now();
    std::string transcript = stt.transcribe(resampled, 16000);
    auto stt_end = std::chrono::steady_clock::now();
    log_info() << "  STT time: " << format_duration(stt_end - stt_start) << "\n";

    if (transcript.empty()) {
        log_error() << "Transcription produced no output\n";
        write_file(output_dir + "/transcription.txt", "");
        write_file(output_dir + "/summary.txt", "Transcription failed.\n");
        return false;
    }

    log_info() << "  Transcription: " << transcript << "\n";
    write_file(output_dir + "/transcription.txt", transcript + "\n");

    // Detect command
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

    // Read ASCII art
    std::string ascii_art;
    {
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
    write_file(output_dir + "/scores.txt", format_scores(detection, totals, audio_stats));
    write_file(output_dir + "/summary.txt",
               format_summary(detection, totals, cfg, input_file, transcript, ascii_art));

    // Launch DOOM if command detected
    if (totals.command_detected) {
        log_info() << "Command detected! Launching DOOM...\n";
        if (!ascii_art.empty()) {
            std::cout << ascii_art << std::endl;
        }
        int result = run_doom(args.doom_binary, args.demos_dir, output_dir,
                              cfg.doom_frames, cfg.doom_maxframes,
                              cfg.doom_keepgifframes, cfg.doom_demo_order);
        if (result != 0) {
            log_warning() << "DOOM had " << result << " failure(s)\n";
        }
    } else {
        log_info() << "No command detected.\n";
    }

    auto proc_end = std::chrono::steady_clock::now();
    log_info() << "Processing time: " << format_duration(proc_end - proc_start) << "\n";
    return totals.command_detected;
}

int main(int argc, char** argv) {
    Args args;
    if (!parse_args(argc, argv, args)) {
        return 1;
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Redirect stdout and stderr to a log file in the output directory
    // so that both manual runs and run-script runs produce the same artifacts.
    {
        std::string mkdir_cmd = "mkdir -p " + args.output_dir;
        system(mkdir_cmd.c_str());
        std::string log_path = args.output_dir + "/pretty-doomed.log";
        FILE* log_fp = freopen(log_path.c_str(), "w", stdout);
        if (!log_fp) {
            std::cerr << "Warning: cannot open " << log_path << " for logging\n";
        } else {
            // Redirect stderr to the same file
            dup2(fileno(stdout), fileno(stderr));
        }
    }

    log_info() << "=== PRETTY DOOMed ===\n";
    auto pipeline_start = std::chrono::steady_clock::now();

    // Load config
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
        log_info() << "  Decoding: " << cfg.decoding_method << "\n";
        log_info() << "  Fuzzy distance: " << cfg.fuzzy_max_distance << "\n";
        log_info() << "  Variants: " << variants.size() << " entries\n";
    }

    // Load STT model once (reused across all captures/processing)
    Transcriber stt(cfg);
    if (!stt.is_ready()) {
        log_error() << "STT model failed to load\n";
        return 1;
    }

    bool any_detected = false;

    if (args.sdr_capture) {
        // Multi-capture mode: capture all, then process all (sequential)
        int num_captures = cfg.sdr_captures;
        if (num_captures <= 0) num_captures = 1;

        log_info() << "SDR Capture mode: " << num_captures << " capture(s) of "
                   << cfg.sdr_duration << "s\n";

        // Phase 1: Capture all (each capture logs to its own run.log)
        std::vector<CaptureResult> captures;
        for (int i = 1; i <= num_captures && g_running; i++) {
            char subdir[32];
            std::snprintf(subdir, sizeof(subdir), "/capture-%03d", i);
            std::string capture_dir = args.output_dir + subdir;

            std::string mkdir_cmd = "mkdir -p " + capture_dir;
            system(mkdir_cmd.c_str());

            log_info() << "Capture " << i << "/" << num_captures << "\n";

            // Switch to per-capture log
            int saved_fd = switch_log(capture_dir + "/run.log");

            log_info() << "=== Capture " << i << "/" << num_captures << " ===\n";
            CaptureResult result;
            if (!run_capture(cfg, capture_dir, result)) {
                log_error() << "Capture " << i << " failed\n";
            }
            captures.push_back(result);

            // Restore top-level log
            restore_log(saved_fd);

            if (i < num_captures && g_running) {
                log_info() << "Waiting 5s for IIO cleanup...\n";
                std::this_thread::sleep_for(std::chrono::seconds(5));
            }
        }

        log_info() << "All captures complete, processing " << captures.size() << " WAV(s)\n";

        // Phase 2: Process each captured WAV (appends to each capture's run.log)
        for (size_t i = 0; i < captures.size() && g_running; i++) {
            if (!captures[i].success && captures[i].wav_path.empty()) {
                log_warning() << "Skipping capture " << (i + 1) << " (no WAV produced)\n";
                continue;
            }

            char subdir[32];
            std::snprintf(subdir, sizeof(subdir), "/capture-%03d", (int)(i + 1));
            std::string capture_dir = args.output_dir + subdir;

            log_info() << "Processing capture " << (i + 1) << "/" << captures.size() << "\n";

            // Switch to per-capture log (append)
            int saved_fd = dup(fileno(stdout));
            std::fflush(stdout);
            std::fflush(stderr);
            FILE* fp = freopen((capture_dir + "/run.log").c_str(), "a", stdout);
            if (fp) dup2(fileno(stdout), fileno(stderr));

            if (process_wav(captures[i].wav_path, capture_dir, cfg, variants, args, stt)) {
                any_detected = true;
            }

            // Restore top-level log
            restore_log(saved_fd);
        }
    } else {
        // Single file mode
        any_detected = process_wav(args.input_file, args.output_dir, cfg, variants, args, stt);
    }

    auto pipeline_end = std::chrono::steady_clock::now();
    log_info() << "Total time: " << format_duration(pipeline_end - pipeline_start) << "\n";
    log_info() << "=== Done ===\n";
    return any_detected ? 0 : 2;
}
