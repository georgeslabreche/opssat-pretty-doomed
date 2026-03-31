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
#include <string>
#include <chrono>
#include <future>
#include <thread>
#include <vector>
#include <memory>
#include <getopt.h>

#include <csignal>
#include <cstdio>
#include <unistd.h>

#include "pretty_log.h"
#include "pretty_signal.h"

#include <iio.h>
#include <ad9361.h>

#include "config.h"
#include "transcriber.h"
#include "capture.h"
#include "pipeline.h"

using namespace pretty;

static std::string format_duration(std::chrono::steady_clock::duration d) {
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(d).count();
    return std::to_string(ms / 1000) + "." + std::to_string((ms % 1000) / 100) + "s";
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

int main(int argc, char** argv) {
    Args args;
    if (!parse_args(argc, argv, args)) {
        return 1;
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Redirect stdout and stderr to a log file in the output directory
    {
        std::string mkdir_cmd = "mkdir -p " + args.output_dir;
        system(mkdir_cmd.c_str());
        std::string log_path = args.output_dir + "/pretty-doomed.log";
        FILE* log_fp = freopen(log_path.c_str(), "w", stdout);
        if (!log_fp) {
            std::cerr << "Warning: cannot open " << log_path << " for logging\n";
        } else {
            dup2(fileno(stdout), fileno(stderr));
        }
    }

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
    log_info() << "=== PRETTY DOOMed v" TOSTRING(APP_VERSION) " ===\n";
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
        log_info() << "  Wake word: " << cfg.detect_wake_word << "\n";
        log_info() << "  Call signs: " << cfg.detect_call_signs.size() << " configured\n";
        log_info() << "  Decoding: " << cfg.stt_decoding_method << "\n";
        log_info() << "  Fuzzy distance: " << cfg.detect_fuzzy_max_distance << "\n";
        log_info() << "  Variants: " << variants.size() << " entries\n";
    }

    bool any_detected = false;

    if (args.sdr_capture) {
        int num_captures = cfg.sdr_captures;
        if (num_captures <= 0) num_captures = 1;
        bool background = (cfg.process_mode == "background");

        log_info() << "SDR Capture mode: " << num_captures << " capture(s) of "
                   << cfg.sdr_duration << "s, processing=" << cfg.process_mode
                   << ", decimation=" << (cfg.sdr_hw_fir_enable ? "hardware FIR" : "software") << "\n";

        // Background mode: load STT model.
        //   concurrent_load=true:  load on background thread while first capture runs
        //   concurrent_load=false: load before captures (blocks until ready)
        // Sequential mode: defer loading until after all captures complete.
        std::unique_ptr<Transcriber> stt;
        std::future<std::unique_ptr<Transcriber>> stt_future;
        if (background) {
            if (cfg.stt_concurrent_load) {
                log_info() << "Loading STT model (concurrent with capture)...\n";
                stt_future = std::async(std::launch::async, [&cfg]() {
                    return std::make_unique<Transcriber>(cfg);
                });
            } else {
                stt = std::make_unique<Transcriber>(cfg);
                if (!stt->is_ready()) {
                    log_error() << "STT model failed to load\n";
                    return 1;
                }
            }
        }

        // Helper: process a capture's WAV.
        // Sequential mode: redirects output to per-capture run.log.
        // Background mode: output stays in main log (stdout is process-global,
        // can't be safely redirected from a concurrent thread).
        auto do_process = [&](int idx, const CaptureResult& cap) -> bool {
            char subdir[32];
            std::snprintf(subdir, sizeof(subdir), "/capture-%03d", idx);
            std::string capture_dir = args.output_dir + subdir;

            int saved_fd = -1;
            if (!background) {
                saved_fd = dup(fileno(stdout));
                std::fflush(stdout);
                std::fflush(stderr);
                FILE* fp = freopen((capture_dir + "/run.log").c_str(), "a", stdout);
                if (fp) dup2(fileno(stdout), fileno(stderr));
            }

            bool detected = process_wav(cap.wav_path, capture_dir, cfg, variants, *stt,
                                        args.config_file, args.doom_binary, args.demos_dir,
                                        cap.sc16_path);

            if (saved_fd >= 0) {
                restore_log(saved_fd);
            }
            return detected;
        };

        std::vector<CaptureResult> captures;
        std::future<bool> bg_future;
        int bg_capture_idx = 0;
        std::chrono::steady_clock::time_point bg_start;

        for (int i = 1; i <= num_captures && g_running; i++) {
            char subdir[32];
            std::snprintf(subdir, sizeof(subdir), "/capture-%03d", i);
            std::string capture_dir = args.output_dir + subdir;

            std::string mkdir_cmd = "mkdir -p " + capture_dir;
            system(mkdir_cmd.c_str());

            // Sequential mode: redirect output to per-capture log.
            // Background mode: all output stays in the main log with a prefix.
            int saved_fd = -1;
            if (!background) {
                log_info() << "Capture " << i << "/" << num_captures << "\n";
                saved_fd = switch_log(capture_dir + "/run.log");
            }

            log_info() << "=== Capture " << i << "/" << num_captures << " ===\n";
            CaptureResult result;
            if (!run_capture(cfg, capture_dir, result)) {
                log_warning() << "Capture " << i << " failed, retrying in 2s...\n";
                std::this_thread::sleep_for(std::chrono::seconds(2));
                if (!run_capture(cfg, capture_dir, result)) {
                    log_error() << "Capture " << i << " failed on retry\n";
                }
            }
            captures.push_back(result);

            if (saved_fd >= 0) {
                restore_log(saved_fd);
            }

            // Background mode: kick off processing while next capture runs
            if (background && !captures.back().wav_path.empty()) {
                // Ensure STT model is ready before first processing call
                if (!stt && stt_future.valid()) {
                    stt = stt_future.get();
                    if (!stt->is_ready()) {
                        log_error() << "STT model failed to load\n";
                        return 1;
                    }
                }
                if (bg_future.valid()) {
                    if (bg_future.get()) any_detected = true;
                    log_info() << "Background processing of capture " << bg_capture_idx
                               << " complete (" << format_duration(std::chrono::steady_clock::now() - bg_start) << ")\n";
                }
                bg_capture_idx = i;
                bg_start = std::chrono::steady_clock::now();
                int idx = i;
                CaptureResult cap = captures.back();
                bg_future = std::async(std::launch::async, [&, idx, cap]() {
                    return do_process(idx, cap);
                });
                log_info() << "Background processing of capture " << i << " started\n";
            }

        }

        // Wait for final background processing
        if (bg_future.valid()) {
            if (bg_future.get()) any_detected = true;
            log_info() << "Background processing of capture " << bg_capture_idx
                       << " complete (" << format_duration(std::chrono::steady_clock::now() - bg_start) << ")\n";
        }

        if (!background) {
            // Sequential: load STT model now (deferred from before captures)
            stt = std::make_unique<Transcriber>(cfg);
            if (!stt->is_ready()) {
                log_error() << "STT model failed to load\n";
                return 1;
            }

            log_info() << "All captures complete, processing " << captures.size() << " WAV(s)\n";

            for (size_t i = 0; i < captures.size() && g_running; i++) {
                if (captures[i].wav_path.empty()) {
                    log_warning() << "Skipping capture " << (i + 1) << " (no WAV produced)\n";
                    continue;
                }
                log_info() << "Processing capture " << (i + 1) << "/" << captures.size() << "\n";
                if (do_process((int)(i + 1), captures[i])) {
                    any_detected = true;
                }
            }
        }

        // Wait for any outstanding artifact futures (background mode)
        for (auto& cap : captures) {
            if (cap.artifact_future.valid()) {
                cap.artifact_future.get();
            }
        }
    } else {
        // Single file mode: load STT and process
        log_info() << "File input mode, processing=" << cfg.process_mode << "\n";
        Transcriber stt_file(cfg);
        if (!stt_file.is_ready()) {
            log_error() << "STT model failed to load\n";
            return 1;
        }
        any_detected = process_wav(args.input_file, args.output_dir, cfg, variants, stt_file,
                                   args.config_file, args.doom_binary, args.demos_dir);
    }

    // Restore AD9361 to non-FIR state so subsequent experiments aren't affected
    if (args.sdr_capture && cfg.sdr_hw_fir_enable) {
        struct iio_context* cleanup_ctx = iio_create_context_from_uri(cfg.sdr_uri.c_str());
        if (cleanup_ctx) {
            struct iio_device* cleanup_phy = iio_context_find_device(cleanup_ctx, "ad9361-phy");
            if (cleanup_phy) {
                ad9361_set_trx_fir_enable(cleanup_phy, 0);
                int fir_enabled = 0;
                if (ad9361_get_trx_fir_enable(cleanup_phy, &fir_enabled) == 0 && !fir_enabled) {
                    log_info() << "AD9361 hardware FIR disabled (cleanup)\n";
                } else {
                    log_warning() << "AD9361 hardware FIR may still be enabled after cleanup\n";
                }
            }
            iio_context_destroy(cleanup_ctx);
        }
    }

    auto pipeline_end = std::chrono::steady_clock::now();
    log_info() << "Total time: " << format_duration(pipeline_end - pipeline_start) << "\n";
    log_info() << "=== Done ===\n";
    return any_detected ? 0 : 2;
}
