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
#include <atomic>
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

#include "config.h"
#include "transcriber.h"
#include "capture.h"
#include "sdr.h"
#include "pipeline.h"
#include "pretty_spectrogram.h"

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
    std::string sc16_file;
    std::string config_file;
    std::string variants_file;
    std::string output_dir;
    std::string demos_dir;
    std::string doom_binary;
    std::string sc16_replay;
    bool verbose = false;
    bool sdr_capture = false;
};

void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " [options]\n"
              << "\nRequired:\n"
              << "  -i <file>   Input WAV file (not needed with -s)\n"
              << "  -q <file>   Input sc16 file (optional, for postcard I/Q scatter)\n"
              << "  -c <file>   Pipeline config file\n"
              << "  -f <file>   Fuzzy match variants file\n"
              << "  -o <dir>    Output directory\n"
              << "  -d <dir>    DOOM demo files directory\n"
              << "  -e <file>   DOOM binary path\n"
              << "\nOptional:\n"
              << "  -s          SDR capture mode (capture RF audio from AD9361)\n"
              << "  -r <file>   Replay a capture.sc16 through the capture pipeline\n"
              << "              (post-capture processing without SDR hardware)\n"
              << "  -v          Verbose output\n"
              << "  -h          Show this help\n";
}

bool parse_args(int argc, char** argv, Args& args) {
    int opt;
    while ((opt = getopt(argc, argv, "i:q:r:c:f:o:d:e:svh")) != -1) {
        switch (opt) {
            case 'i': args.input_file = optarg; break;
            case 'q': args.sc16_file = optarg; break;
            case 'c': args.config_file = optarg; break;
            case 'f': args.variants_file = optarg; break;
            case 'o': args.output_dir = optarg; break;
            case 'd': args.demos_dir = optarg; break;
            case 'e': args.doom_binary = optarg; break;
            case 'r': args.sc16_replay = optarg; break;
            case 's': args.sdr_capture = true; break;
            case 'v': args.verbose = true; break;
            case 'h': print_usage(argv[0]); return false;
            default: print_usage(argv[0]); return false;
        }
    }

    if (!args.sdr_capture && args.input_file.empty() && args.sc16_replay.empty()) {
        std::cerr << "Error: Input required (use -i <wav>, -r <sc16>, or -s for SDR capture)\n\n";
        print_usage(argv[0]);
        return false;
    }
    if (!args.sc16_replay.empty() && (args.sdr_capture || !args.input_file.empty())) {
        std::cerr << "Error: -r cannot be combined with -i or -s\n\n";
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

    if (args.sdr_capture || !args.sc16_replay.empty()) {
        // sc16 replay (#116): one file, one capture, no SDR hardware; the
        // post-capture processing is identical to a live capture.
        const bool file_replay = !args.sc16_replay.empty();
        int num_captures = file_replay ? 1 : cfg.sdr_captures;
        if (num_captures <= 0) num_captures = 1;
        bool background = (cfg.process_mode == "background");

        if (file_replay) {
            log_info() << "SC16 replay mode: " << args.sc16_replay
                       << ", processing=" << cfg.process_mode << "\n";
        } else
        log_info() << "SDR Capture mode: " << num_captures << " capture(s) of "
                   << cfg.sdr_duration << "s, processing=" << cfg.process_mode
                   << ", decimation=" << (cfg.sdr_hw_fir_enable ? "hardware FIR" : "software")
                   << ", sdr_init=" << (cfg.sdr_init_per_capture ? "per-capture" : "once") << "\n";

        // Configure AD9361 once before the capture loop (default).
        // Per-capture mode skips this and re-inits inside run_capture() instead.
        if (!file_replay && !cfg.sdr_init_per_capture) {
            if (!ad9361_configure(cfg)) {
                log_error() << "AD9361 configuration failed\n";
                return 1;
            }
        }

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

        // Helper: process a capture's WAV (sequential mode only).
        // Redirects output to per-capture run.log, runs full pipeline.
        auto do_process = [&](int idx, const CaptureResult& cap) -> bool {
            char subdir[32];
            std::snprintf(subdir, sizeof(subdir), "/capture-%03d", idx);
            std::string capture_dir = args.output_dir + subdir;

            int saved_fd = dup(fileno(stdout));
            std::fflush(stdout);
            std::fflush(stderr);
            FILE* fp = freopen((capture_dir + "/run.log").c_str(), "a", stdout);
            if (fp) dup2(fileno(stdout), fileno(stderr));

            bool detected = process_wav(cap.wav_path, capture_dir, cfg, variants, *stt,
                                        args.config_file, args.doom_binary, args.demos_dir,
                                        cap.sc16_path, cap.artifact_future);

            restore_log(saved_fd);
            return detected;
        };

        std::vector<CaptureResult> captures;

        // Background mode: STT stage chains sequentially (Transcriber not
        // thread-safe), exec stage (DOOM + postcard) runs async so the next
        // STT can start immediately without waiting for DOOM/postcard.
        std::future<void> stt_chain;
        std::vector<std::future<void>> exec_futures;
        std::atomic<bool> bg_any_detected{false};

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
            if (file_replay) {
                if (!run_capture_from_file(cfg, args.sc16_replay, capture_dir, result)) {
                    log_error() << "SC16 replay failed\n";
                }
            } else if (!run_capture(cfg, capture_dir, result)) {
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

            // Background mode: two-stage pipeline.
            // Stage 1 (STT): chains sequentially, each waits for previous STT.
            // Stage 2 (exec): fires async after STT, runs DOOM + postcard
            //                 concurrently with the next capture's STT.
            if (background && !captures.back().wav_path.empty()) {
                if (!stt && stt_future.valid()) {
                    stt = stt_future.get();
                    if (!stt->is_ready()) {
                        log_error() << "STT model failed to load\n";
                        return 1;
                    }
                }
                int idx = i;
                CaptureResult cap = captures.back();
                std::string cap_dir = capture_dir;
                auto prev_stt = std::move(stt_chain);
                stt_chain = std::async(std::launch::async,
                    [&, idx, cap, cap_dir, prev_stt = std::move(prev_stt)]() mutable {
                        if (prev_stt.valid()) prev_stt.get();
                        log_info() << "Background STT of capture " << idx << " started\n";
                        auto start = std::chrono::steady_clock::now();
                        PipelineStageResult stage1 = process_wav_stt(
                            cap.wav_path, cap_dir, cfg, variants, *stt, args.config_file);
                        log_info() << "Background STT of capture " << idx
                                   << " complete (" << format_duration(std::chrono::steady_clock::now() - start) << ")\n";
                        if (stage1.command_detected) bg_any_detected = true;
                        // Fire exec stage async (DOOM + postcard + sc16 cleanup)
                        exec_futures.push_back(std::async(std::launch::async,
                            [stage1, cap_dir, &cfg, &args, cap]() {
                                process_wav_exec(stage1, cap_dir, cfg,
                                                 args.doom_binary, args.demos_dir,
                                                 cap.sc16_path, cap.artifact_future);
                            }));
                    });
            }

        }

        // Wait for STT chain and all exec futures to complete
        if (stt_chain.valid()) {
            stt_chain.get();
        }
        for (auto& f : exec_futures) {
            if (f.valid()) f.get();
        }
        if (bg_any_detected) any_detected = true;

        // Disable hardware FIR now that all captures are done.
        // Processing uses WAV files, not the AD9361, so free the hardware state
        // sooner for subsequent experiments.
        if (!file_replay) {
            ad9361_cleanup_fir(cfg);
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

        // Cross-capture PSD comparison (reads per-capture CSV files)
        if (cfg.sdr_enable_psd && captures.size() > 1) {
            std::vector<std::string> psd_paths;
            for (const auto& cap : captures) {
                if (cap.sc16_path.empty()) continue;
                std::string psd_csv = make_psd_csv_filename(cap.sc16_path);
                std::ifstream test(psd_csv);
                if (test.good()) psd_paths.push_back(psd_csv);
            }
            if (psd_paths.size() > 1) {
                std::string comp_path = args.output_dir + "/psd-comparison.bmp";
                log_info() << "Generating PSD comparison: " << comp_path << "\n";
                if (!generate_psd_comparison(psd_paths, comp_path)) {
                    log_warning() << "PSD comparison generation failed\n";
                }
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
                                   args.config_file, args.doom_binary, args.demos_dir,
                                   args.sc16_file);
    }

    auto pipeline_end = std::chrono::steady_clock::now();
    log_info() << "Total time: " << format_duration(pipeline_end - pipeline_start) << "\n";
    log_info() << "=== Done ===\n";
    return any_detected ? 0 : 2;
}
