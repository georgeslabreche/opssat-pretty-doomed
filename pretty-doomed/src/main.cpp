/**
 * PRETTY DOOMed - Voice-command-to-DOOM pipeline for OPS-SAT
 *
 * Pipeline: WAV -> Lowpass -> Bandpass -> Resample -> STT -> Match -> DOOM
 */

#include <iostream>
#include <fstream>
#include <string>
#include <ctime>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <getopt.h>

#include "config.h"
#include "audio_io.h"
#include "dsp.h"
#include "transcriber.h"
#include "matcher.h"
#include "output.h"
#include "executor.h"

static std::string timestamp() {
    time_t now = time(nullptr);
    char buf[20];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&now));
    return buf;
}

static void log(const std::string& msg) {
    std::cout << "[" << timestamp() << "] " << msg << std::endl;
}

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
};

void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " [options]\n"
              << "\nRequired:\n"
              << "  -i <file>   Input WAV file\n"
              << "  -c <file>   Pipeline config file\n"
              << "  -f <file>   Fuzzy match variants file\n"
              << "  -o <dir>    Output directory\n"
              << "  -d <dir>    DOOM demo files directory\n"
              << "  -e <file>   DOOM binary path\n"
              << "\nOptional:\n"
              << "  -v          Verbose output\n"
              << "  -h          Show this help\n";
}

bool parse_args(int argc, char** argv, Args& args) {
    int opt;
    while ((opt = getopt(argc, argv, "i:c:f:o:d:e:vh")) != -1) {
        switch (opt) {
            case 'i': args.input_file = optarg; break;
            case 'c': args.config_file = optarg; break;
            case 'f': args.variants_file = optarg; break;
            case 'o': args.output_dir = optarg; break;
            case 'd': args.demos_dir = optarg; break;
            case 'e': args.doom_binary = optarg; break;
            case 'v': args.verbose = true; break;
            case 'h': print_usage(argv[0]); return false;
            default: print_usage(argv[0]); return false;
        }
    }

    if (args.input_file.empty() ||
        args.config_file.empty() || args.variants_file.empty() ||
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
        std::cerr << "Warning: Cannot write to " << path << std::endl;
    }
}

int main(int argc, char** argv) {
    Args args;
    if (!parse_args(argc, argv, args)) {
        return 1;
    }

    log("=== PRETTY DOOMed ===");
    auto pipeline_start = std::chrono::steady_clock::now();

    // Step 1: Load config
    log("Loading config: " + args.config_file);
    PipelineConfig cfg;
    if (!load_config(args.config_file, cfg)) {
        std::cerr << "Error: Cannot load config: " << args.config_file << std::endl;
        return 1;
    }

    VariantsMap variants;
    if (!load_variants(args.variants_file, variants)) {
        std::cerr << "Error: Cannot load variants: " << args.variants_file << std::endl;
        return 1;
    }

    if (args.verbose) {
        log("  Wake word: " + cfg.wake_word);
        log("  Call signs: " + std::to_string(cfg.call_signs.size()) + " configured");
        std::string cmds_str = "  Commands: ";
        for (size_t i = 0; i < cfg.commands.size(); i++) {
            if (i > 0) cmds_str += ", ";
            cmds_str += cfg.commands[i];
        }
        log(cmds_str);
        log("  Decoding: " + cfg.decoding_method);
        log("  Fuzzy distance: " + std::to_string(cfg.fuzzy_max_distance));
        log("  Variants: " + std::to_string(variants.size()) + " entries");
    }

    // Step 2: Read audio
    log("Reading audio: " + args.input_file);
    std::vector<float> samples;
    int sample_rate;
    if (!read_wav(args.input_file, samples, sample_rate)) {
        return 1;
    }
    log("  " + std::to_string(sample_rate) + " Hz, " +
        std::to_string(samples.size()) + " samples (" +
        std::to_string(static_cast<float>(samples.size()) / sample_rate) + "s)");

    AudioStats audio_stats;
    compute_audio_stats(samples, audio_stats.rms_raw, audio_stats.peak_raw);

    // Step 3: GNU Radio signal processing
    log("Filtering (GNU Radio)...");
    if (args.verbose) {
        log("  Lowpass: " + std::to_string(cfg.lowpass_cutoff) + " Hz");
        log("  Bandpass: " + std::to_string(cfg.bandpass_low) + "-" +
            std::to_string(cfg.bandpass_high) + " Hz");
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
    log("  Denoised audio: " + denoised_path);

    // Step 4: Resample to 16 kHz
    log("Resampling to 16 kHz...");
    auto resampled = resample(filtered, sample_rate, 16000);
    auto dsp_end = std::chrono::steady_clock::now();
    log("  " + std::to_string(resampled.size()) + " samples");
    log("  DSP time: " + format_duration(dsp_end - dsp_start));

    // Step 5: Transcribe
    log("Transcribing (" + cfg.decoding_method + ")...");
    auto stt_start = std::chrono::steady_clock::now();
    std::string transcript = transcribe(resampled, 16000, cfg);
    auto stt_end = std::chrono::steady_clock::now();
    log("  STT time: " + format_duration(stt_end - stt_start));

    if (transcript.empty()) {
        std::cerr << "Error: Transcription produced no output" << std::endl;
        // Still write empty files for diagnostics
        write_file(args.output_dir + "/transcription.txt", "");
        write_file(args.output_dir + "/summary.txt", "Transcription failed.\n");
        return 1;
    }

    log("  Transcription: " + transcript);
    write_file(args.output_dir + "/transcription.txt", transcript + "\n");

    // Step 6: Detect command
    log("Detecting command...");
    DetectionResult detection = detect(transcript, cfg, variants);

    int wake_total = detection.wake_word_exact + detection.wake_word_approx;
    log("  Wake word: " + std::to_string(wake_total) +
        " (exact=" + std::to_string(detection.wake_word_exact) +
        ", approx=" + std::to_string(detection.wake_word_approx) + ")");
    for (const auto& [cmd, ec] : detection.command_exact_counts) {
        int ac = 0;
        auto it = detection.command_approx_counts.find(cmd);
        if (it != detection.command_approx_counts.end()) ac = it->second;
        log("  Command [" + cmd + "]: " + std::to_string(ec + ac) +
            " (exact=" + std::to_string(ec) + ", approx=" + std::to_string(ac) + ")");
    }
    for (const auto& [cmd, ac] : detection.command_approx_counts) {
        if (detection.command_exact_counts.count(cmd) == 0) {
            log("  Command [" + cmd + "]: " + std::to_string(ac) +
                " (exact=0, approx=" + std::to_string(ac) + ")");
        }
    }
    for (const auto& [sign, count] : detection.call_sign_counts) {
        log("  Call sign [" + sign + "]: " + std::to_string(count));
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
        log("Command detected! Launching DOOM...");
        if (!ascii_art.empty()) {
            std::cout << ascii_art << std::endl;
        }
        int result = run_doom(args.doom_binary, args.demos_dir, args.output_dir,
                              cfg.doom_frames, cfg.doom_maxframes,
                              cfg.doom_keepgifframes);
        if (result != 0) {
            std::cerr << "Warning: DOOM had " << result << " failure(s)" << std::endl;
        }
    } else {
        log("No command detected.");
    }

    auto pipeline_end = std::chrono::steady_clock::now();
    log("Total time: " + format_duration(pipeline_end - pipeline_start));
    log("=== Done ===");
    return totals.command_detected ? 0 : 2;
}
