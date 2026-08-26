#include "pipeline.h"

#include <cstdio>
#include <iostream>
#include <fstream>
#include <string>
#include <chrono>
#include <cmath>
#include <vector>

#include "pretty_log.h"

#include "audio_io.h"
#include "pretty_resample.h"
#include "matcher.h"
#include "output.h"
#include "executor.h"
#include "postcard.h"

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

static void write_file(const std::string& path, const std::string& content) {
    std::ofstream out(path);
    if (out) {
        out << content;
    } else {
        log_warning() << "Cannot write to " << path << "\n";
    }
}

PipelineStageResult process_wav_stt(const std::string& input_file,
                                    const std::string& output_dir,
                                    const PipelineConfig& cfg,
                                    const VariantsMap& variants,
                                    Transcriber& stt,
                                    const std::string& config_file) {
    PipelineStageResult result;

    log_info() << "--- Processing: " << input_file << " ---\n";

    // Read audio
    log_info() << "Reading audio: " << input_file << "\n";
    std::vector<float> samples;
    int sample_rate;
    if (!read_wav(input_file, samples, sample_rate)) {
        return result;
    }
    log_info() << "  " << sample_rate << " Hz, " << samples.size()
               << " samples (" << static_cast<float>(samples.size()) / sample_rate << "s)\n";

    AudioStats audio_stats;
    compute_audio_stats(samples, audio_stats.rms_raw, audio_stats.peak_raw);

    // Resample straight to the model rate. The second-stage filtering that
    // used to run here (low-pass + band-pass ahead of the STT) was removed
    // (#108): capture audio is already band-passed at capture, and the second
    // pass cost recognition margin on marginal audio.
    auto dsp_start = std::chrono::steady_clock::now();
    log_info() << "Resampling to 16 kHz...\n";
    auto resampled = resample(samples, sample_rate, 16000);
    auto dsp_end = std::chrono::steady_clock::now();
    log_info() << "  " << resampled.size() << " samples, DSP time: "
               << format_duration(dsp_end - dsp_start) << "\n";

    // STT (minimum ~0.5s of audio needed for the model's Conv layers)
    if (resampled.size() < 8000) {
        log_warning() << "Audio too short for STT (" << resampled.size()
                      << " samples, need at least 8000). Skipping transcription.\n";
        write_file(output_dir + "/transcription.txt", "");
        write_file(output_dir + "/summary.txt", "Audio too short for transcription.\n");
        return result;
    }

    log_info() << "Transcribing (" << cfg.stt_decoding_method << ")...\n";
    auto stt_start = std::chrono::steady_clock::now();
    result.transcript = stt.transcribe(resampled, 16000);
    auto stt_end = std::chrono::steady_clock::now();
    log_info() << "  STT time: " << format_duration(stt_end - stt_start) << "\n";

    if (result.transcript.empty()) {
        log_error() << "Transcription produced no output\n";
        write_file(output_dir + "/transcription.txt", "");
        write_file(output_dir + "/summary.txt", "Transcription failed.\n");
        return result;
    }

    log_info() << "  Transcription: " << result.transcript << "\n";
    write_file(output_dir + "/transcription.txt", result.transcript + "\n");

    // Detect command
    log_info() << "Detecting command...\n";
    DetectionResult detection = detect(result.transcript, cfg, variants);

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
        std::string cfg_dir = config_file;
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
               format_summary(detection, totals, cfg, input_file, result.transcript, ascii_art));

    result.command_detected = totals.command_detected;
    result.trigger = totals.command_detected || cfg.doom_force_trigger;
    result.ascii_art = ascii_art;

    if (cfg.doom_force_trigger && !totals.command_detected) {
        log_info() << "Force-triggering (doom_force_trigger=true)\n";
    }

    // Capture detection timestamp for the execution stage
    if (result.trigger) {
        time_t det_time = time(nullptr);
        struct tm* det_tm = gmtime(&det_time);
        char det_ts[48];
        snprintf(det_ts, sizeof(det_ts),
            "%04d-%02d-%02d %02d:%02d:%02d UTC",
            det_tm->tm_year + 1900, det_tm->tm_mon + 1, det_tm->tm_mday,
            det_tm->tm_hour, det_tm->tm_min, det_tm->tm_sec);
        result.detection_timestamp = det_ts;
    }

    return result;
}

void process_wav_exec(const PipelineStageResult& stage1,
                      const std::string& output_dir,
                      const PipelineConfig& cfg,
                      const std::string& doom_binary,
                      const std::string& demos_dir,
                      const std::string& sc16_path,
                      std::shared_future<void> artifact_future) {
    // SC16 cleanup: wait for artifact future then delete (unless the retention
    // policy keeps this capture). Runs regardless of trigger -- even captures
    // with no detection should clean up. The decision itself is the pure
    // should_keep_sc16() (config.h), unit-tested in tests/test_config.cpp.
    auto cleanup_sc16 = [&]() {
        if (sc16_path.empty()) return;
        if (should_keep_sc16(cfg.sdr_keep_sc16, stage1.command_detected)) {
            if (cfg.sdr_keep_sc16 == Sc16Keep::Detected) {
                log_info() << "SC16 kept (command detected): " << sc16_path << "\n";
            }
            return;
        }
        if (artifact_future.valid()) artifact_future.get();
        if (std::remove(sc16_path.c_str()) == 0) {
            log_info() << "SC16 deleted: " << sc16_path << "\n";
        } else {
            log_warning() << "SC16 delete failed: " << sc16_path << "\n";
        }
    };

    if (!stage1.trigger) {
        if (!stage1.transcript.empty()) {
            log_info() << "No command detected.\n";
        }
        cleanup_sc16();
        return;
    }

    if (cfg.operation != "doom") {
        log_warning() << "Unknown operation: " << cfg.operation << "\n";
        return;
    }

    log_info() << "Command detected! Launching DOOM...\n";
    if (!stage1.ascii_art.empty()) {
        std::cout << stage1.ascii_art << std::endl;
    }

    // NOTE: exec stages can run concurrently if DOOM + Postcard for capture N
    // takes longer than STT for capture N+1. This means run_doom() calls could
    // overlap, creating a race on doom_demo_index.txt (demo cycling state).
    // On the EM this is unlikely (~40s STT vs ~13-41s DOOM+Postcard) but not
    // impossible. If demo ordering matters, serialize exec stages or pre-assign
    // demo indices during the STT stage.
    DoomResult doom_result = run_doom(doom_binary, demos_dir, output_dir,
                                      cfg.doom_frames, cfg.doom_maxframes,
                                      cfg.doom_keepgifframes, cfg.doom_demo_order);
    if (doom_result.failures != 0) {
        log_warning() << "DOOM had " << doom_result.failures << " failure(s)\n";
    }

    // Generate postcard
    if (cfg.doom_enable_postcard && !doom_result.demo_dir.empty()) {
        std::string frame = find_doom_frame(doom_result.demo_dir);
        if (!frame.empty()) {
            PostcardArgs pargs;
            pargs.frame_path = frame;
            pargs.sc16_path = sc16_path;
            pargs.transcription = stage1.transcript;
            pargs.demo_name = doom_result.demo_name;
            pargs.timestamp = stage1.detection_timestamp;
            pargs.logo_esa = cfg.doom_assets_dir + "/logo-esa.png";
            pargs.logo_doom = cfg.doom_assets_dir + "/logo-doom.png";
            pargs.logo_pretty = cfg.doom_assets_dir + "/logo-opssat-pretty.png";
            pargs.output_path = output_dir + "/postcard.png";
            pargs.scale = cfg.doom_postcard_scale;

            if (!generate_postcard(pargs)) {
                log_warning() << "Postcard generation failed\n";
            }
        } else {
            log_warning() << "No DOOM frame found, skipping postcard\n";
        }
    }

    // Append to toGround/results.txt
    {
        std::string results_dir = output_dir;
        for (int i = 0; i < 3; i++) {
            auto slash = results_dir.find_last_of('/');
            if (slash == std::string::npos) break;
            std::string dirname = results_dir.substr(slash + 1);
            if (dirname.find("capture-") != 0 && dirname.find("run-") != 0) break;
            results_dir = results_dir.substr(0, slash);
        }
        std::string results_path = results_dir + "/results.txt";
        std::ofstream results(results_path, std::ios::app);
        if (results) {
            std::string trigger_type = stage1.command_detected ? "detected" : "force";
            results << stage1.detection_timestamp
                    << " | " << output_dir
                    << " | demo=" << doom_result.demo_name
                    << " | trigger=" << trigger_type
                    << " | failures=" << doom_result.failures
                    << " | transcript=" << stage1.transcript
                    << "\n";
        }
    }

    cleanup_sc16();
}

bool process_wav(const std::string& input_file,
                 const std::string& output_dir,
                 const PipelineConfig& cfg,
                 const VariantsMap& variants,
                 Transcriber& stt,
                 const std::string& config_file,
                 const std::string& doom_binary,
                 const std::string& demos_dir,
                 const std::string& sc16_path,
                 std::shared_future<void> artifact_future) {
    auto proc_start = std::chrono::steady_clock::now();

    PipelineStageResult stage1 = process_wav_stt(input_file, output_dir, cfg, variants,
                                                  stt, config_file);
    process_wav_exec(stage1, output_dir, cfg, doom_binary, demos_dir, sc16_path,
                     artifact_future);

    auto proc_end = std::chrono::steady_clock::now();
    log_info() << "Pipeline time: " << format_duration(proc_end - proc_start)
               << " (resample + STT + detection)\n";
    return stage1.command_detected;
}
