#include "pipeline.h"

#include <iostream>
#include <fstream>
#include <string>
#include <chrono>
#include <cmath>
#include <vector>

#include "pretty_log.h"

#include "audio_io.h"
#include "dsp.h"
#include "matcher.h"
#include "output.h"
#include "executor.h"

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

bool process_wav(const std::string& input_file,
                 const std::string& output_dir,
                 const PipelineConfig& cfg,
                 const VariantsMap& variants,
                 Transcriber& stt,
                 const std::string& config_file,
                 const std::string& doom_binary,
                 const std::string& demos_dir) {
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
               format_summary(detection, totals, cfg, input_file, transcript, ascii_art));

    // Launch DOOM if command detected
    if (totals.command_detected) {
        log_info() << "Command detected! Launching DOOM...\n";
        if (!ascii_art.empty()) {
            std::cout << ascii_art << std::endl;
        }
        int result = run_doom(doom_binary, demos_dir, output_dir,
                              cfg.doom_frames, cfg.doom_maxframes,
                              cfg.doom_keepgifframes, cfg.doom_demo_order);
        if (result != 0) {
            log_warning() << "DOOM had " << result << " failure(s)\n";
        }
    } else {
        log_info() << "No command detected.\n";
    }

    auto proc_end = std::chrono::steady_clock::now();
    log_info() << "Pipeline time: " << format_duration(proc_end - proc_start)
               << " (DSP + STT + detection)\n";
    return totals.command_detected;
}
