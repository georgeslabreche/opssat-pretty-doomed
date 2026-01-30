/**
 * PRETTY DOOMed - Voice-command-to-DOOM pipeline for OPS-SAT
 *
 * Pipeline: WAV -> Lowpass -> Bandpass -> Resample -> STT -> Match -> DOOM
 */

#include <iostream>
#include <fstream>
#include <string>
#include <ctime>
#include <getopt.h>

#include "config.h"
#include "audio_io.h"
#include "dsp.h"
#include "transcriber.h"
#include "matcher.h"
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

    // Step 3: GNU Radio signal processing
    log("Filtering (GNU Radio)...");
    if (args.verbose) {
        log("  Lowpass: " + std::to_string(cfg.lowpass_cutoff) + " Hz");
        log("  Bandpass: " + std::to_string(cfg.bandpass_low) + "-" +
            std::to_string(cfg.bandpass_high) + " Hz");
    }

    auto filtered = apply_lowpass(samples, sample_rate,
                                   cfg.lowpass_cutoff, cfg.lowpass_transition);
    filtered = apply_bandpass(filtered, sample_rate,
                               cfg.bandpass_low, cfg.bandpass_high,
                               cfg.bandpass_transition);

    // Write denoised audio
    std::string denoised_path = args.output_dir + "/processed.wav";
    write_wav(denoised_path, filtered, sample_rate);
    log("  Denoised audio: " + denoised_path);

    // Step 4: Resample to 16 kHz
    log("Resampling to 16 kHz...");
    auto resampled = resample(filtered, sample_rate, 16000);
    log("  " + std::to_string(resampled.size()) + " samples");

    // Step 5: Transcribe
    log("Transcribing (" + cfg.decoding_method + ")...");
    std::string transcript = transcribe(resampled, 16000, cfg);

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

    // Total command counts
    int total_command_exact = 0;
    int total_command_approx = 0;
    for (const auto& [cmd, count] : detection.command_exact_counts) {
        total_command_exact += count;
    }
    for (const auto& [cmd, count] : detection.command_approx_counts) {
        total_command_approx += count;
    }
    int total_command_count = total_command_exact + total_command_approx;
    int total_points_exact = detection.wake_word_exact + total_command_exact;
    int total_points_approx = detection.wake_word_approx + total_command_approx;
    int total_points = total_points_exact + total_points_approx;

    auto join = [](const std::vector<std::string>& v, const std::string& sep) {
        std::string s;
        for (size_t i = 0; i < v.size(); i++) {
            if (i > 0) s += sep;
            s += v[i];
        }
        return s;
    };

    auto to_key = [](const std::string& s) {
        std::string k = s;
        for (auto& c : k) { if (c == ' ') c = '_'; }
        return k;
    };

    // Collect all command names (union of exact and approx keys)
    std::vector<std::string> all_cmds;
    for (const auto& [cmd, _] : detection.command_exact_counts) {
        all_cmds.push_back(cmd);
    }
    for (const auto& [cmd, _] : detection.command_approx_counts) {
        if (detection.command_exact_counts.count(cmd) == 0) {
            all_cmds.push_back(cmd);
        }
    }

    // Write scores
    {
        std::string scores;
        scores += "wake_word_exact=" + std::to_string(detection.wake_word_exact) + "\n";
        scores += "wake_word_exact_matches=" + join(detection.wake_word_exact_matches, ",") + "\n";
        scores += "wake_word_approx=" + std::to_string(detection.wake_word_approx) + "\n";
        scores += "wake_word_approx_matches=" + join(detection.wake_word_approx_matches, ",") + "\n";
        for (const auto& cmd : all_cmds) {
            int ec = 0, ac = 0;
            auto eit = detection.command_exact_counts.find(cmd);
            if (eit != detection.command_exact_counts.end()) ec = eit->second;
            auto ait = detection.command_approx_counts.find(cmd);
            if (ait != detection.command_approx_counts.end()) ac = ait->second;

            std::string key = "command_" + to_key(cmd);
            scores += key + "_exact=" + std::to_string(ec) + "\n";
            auto emit = detection.command_exact_matches.find(cmd);
            scores += key + "_exact_matches=" +
                      (emit != detection.command_exact_matches.end() ? join(emit->second, ",") : "") + "\n";
            scores += key + "_approx=" + std::to_string(ac) + "\n";
            auto amit = detection.command_approx_matches.find(cmd);
            scores += key + "_approx_matches=" +
                      (amit != detection.command_approx_matches.end() ? join(amit->second, ",") : "") + "\n";
        }
        for (const auto& [sign, count] : detection.call_sign_counts) {
            scores += "call_sign_" + sign + "=" + std::to_string(count) + "\n";
            auto it = detection.call_sign_matches.find(sign);
            if (it != detection.call_sign_matches.end()) {
                scores += "call_sign_" + sign + "_matches=" + join(it->second, ",") + "\n";
            }
        }
        scores += "total_points=" + std::to_string(total_points) + "\n";
        scores += "total_points_exact=" + std::to_string(total_points_exact) + "\n";
        scores += "total_points_approx=" + std::to_string(total_points_approx) + "\n";
        write_file(args.output_dir + "/scores.txt", scores);
    }

    // Write summary
    {
        std::string summary;
        summary += "=== PRETTY DOOMed Summary ===\n\n";
        summary += "Input: " + args.input_file + "\n";
        summary += "Transcription: " + transcript + "\n\n";
        summary += "Detection:\n";
        summary += "  Wake word (" + cfg.wake_word + "):\n";
        summary += "    Exact: " + std::to_string(detection.wake_word_exact) +
                   " [" + join(detection.wake_word_exact_matches, ", ") + "]\n";
        summary += "    Approximate: " + std::to_string(detection.wake_word_approx) +
                   " [" + join(detection.wake_word_approx_matches, ", ") + "]\n";
        for (const auto& cmd : all_cmds) {
            int ec = 0, ac = 0;
            auto eit = detection.command_exact_counts.find(cmd);
            if (eit != detection.command_exact_counts.end()) ec = eit->second;
            auto ait = detection.command_approx_counts.find(cmd);
            if (ait != detection.command_approx_counts.end()) ac = ait->second;

            summary += "  Command [" + cmd + "]:\n";
            auto emit = detection.command_exact_matches.find(cmd);
            summary += "    Exact: " + std::to_string(ec) + " [" +
                       (emit != detection.command_exact_matches.end() ? join(emit->second, ", ") : "") + "]\n";
            auto amit = detection.command_approx_matches.find(cmd);
            summary += "    Approximate: " + std::to_string(ac) + " [" +
                       (amit != detection.command_approx_matches.end() ? join(amit->second, ", ") : "") + "]\n";
        }
        for (const auto& [sign, count] : detection.call_sign_counts) {
            summary += "  Call sign [" + sign + "]: " + std::to_string(count) + "\n";
            auto it = detection.call_sign_matches.find(sign);
            if (it != detection.call_sign_matches.end()) {
                summary += "    Matches: " + join(it->second, ", ") + "\n";
            }
        }
        summary += "\nTotal points: " + std::to_string(total_points) +
                   " (exact=" + std::to_string(total_points_exact) +
                   ", approx=" + std::to_string(total_points_approx) + ")\n\n";

        if (total_command_count >= 1) {
            summary += "Result: COMMAND DETECTED - launching DOOM\n";
        } else {
            summary += "Result: No command detected\n";
        }

        write_file(args.output_dir + "/summary.txt", summary);
    }

    // Step 7: Launch DOOM if command detected
    if (total_command_count >= 1) {
        log("Command detected! Launching DOOM...");
        int result = run_doom(args.doom_binary, args.demos_dir, args.output_dir);
        if (result != 0) {
            std::cerr << "Warning: DOOM had " << result << " failure(s)" << std::endl;
        }
    } else {
        log("No command detected.");
    }

    log("=== Done ===");
    return 0;
}
