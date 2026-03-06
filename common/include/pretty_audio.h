/*
 * pretty_audio.h - Audio and I/Q file utilities for OPS-SAT PRETTY
 *
 * Header-only. RMS normalization, sc16 quality checks, filename helpers.
 */
#ifndef PRETTY_AUDIO_H
#define PRETTY_AUDIO_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include <sndfile.h>

#include "pretty_log.h"

namespace pretty {

// Maps |x|~1.0 (nominal fc32 magnitude) -> 8192.
// Leaves ~12 dB headroom before int16 rails (+/-32767).
constexpr float IQ_SCALE = 8192.0f;

// Derive .sc16 filename from a .wav filename
inline std::string make_iq_filename(const std::string& wav_path) {
    std::string iq_file = wav_path;
    size_t dot_pos = iq_file.rfind('.');
    size_t sep_pos = iq_file.find_last_of("/\\");
    bool has_extension = (dot_pos != std::string::npos) &&
                         (sep_pos == std::string::npos || dot_pos > sep_pos + 1);
    if (has_extension) {
        iq_file = iq_file.substr(0, dot_pos) + ".sc16";
    } else {
        iq_file += ".sc16";
    }
    return iq_file;
}

// RMS normalize a WAV file to target_dbfs (e.g. -20 dBFS -> target_rms = 0.1)
inline bool rms_normalize(const std::string& wav_path, double target_dbfs) {
    double target_rms = std::pow(10.0, target_dbfs / 20.0);

    SF_INFO sf_info = {0};
    SNDFILE* sf = sf_open(wav_path.c_str(), SFM_READ, &sf_info);
    if (!sf) {
        log_error() << "RMS normalize: Could not open " << wav_path << ": " << sf_strerror(NULL) << "\n";
        return false;
    }
    if (sf_info.channels <= 0) {
        log_error() << "RMS normalize: invalid channel count\n";
        sf_close(sf);
        return false;
    }

    // Read all samples
    std::vector<float> samples((size_t)sf_info.frames * (size_t)sf_info.channels);
    sf_count_t read = sf_readf_float(sf, samples.data(), sf_info.frames);
    sf_close(sf);

    if (read <= 0) {
        log_error() << "RMS normalize: No samples read from " << wav_path << "\n";
        return false;
    }
    size_t valid = (size_t)read * (size_t)sf_info.channels;
    samples.resize(valid);

    // Compute RMS over actual read samples only
    double sum_sq = 0.0;
    for (size_t i = 0; i < valid; i++) {
        sum_sq += (double)samples[i] * samples[i];
    }
    double rms = std::sqrt(sum_sq / valid);

    if (rms < 1e-10) {
        log_error() << "RMS normalize: Signal is silent, skipping normalization\n";
        return true;
    }

    // Scale to target RMS
    double scale = target_rms / rms;
    log_info() << "RMS normalize: rms=" << rms << ", scale=" << scale
               << ", target=" << target_dbfs << " dBFS\n";

    for (size_t i = 0; i < samples.size(); i++) {
        float s = samples[i] * (float)scale;
        // Soft-clip at +/-1.0
        if (s > 1.0f) s = 1.0f;
        else if (s < -1.0f) s = -1.0f;
        samples[i] = s;
    }

    // Rewrite WAV
    SF_INFO out_info = sf_info;
    out_info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
    SNDFILE* out = sf_open(wav_path.c_str(), SFM_WRITE, &out_info);
    if (!out) {
        log_error() << "RMS normalize: Could not write " << wav_path << ": " << sf_strerror(NULL) << "\n";
        return false;
    }
    sf_writef_float(out, samples.data(), read);
    sf_close(out);

    return true;
}

// Check sc16 file for clipping and peak magnitude. Reads first and last
// `check_seconds` worth of samples. Sets clip_rate and peak_abs (0-32768 scale).
inline void check_sc16_quality(const std::string& path, long long sample_rate,
                               double& clip_rate, int& peak_abs, double check_seconds = 2.0) {
    clip_rate = -1.0;
    peak_abs = 0;

    if (sample_rate <= 0) return;

    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) return;

    long long file_bytes = (long long)f.tellg();
    if (file_bytes < 0) return;
    if (file_bytes % 4 != 0)
        log_warning() << "sc16 file not multiple of 4 bytes (truncated?)\n";
    long long total_pairs = file_bytes / 4;  // 2 shorts per complex sample
    if (total_pairs == 0) return;
    long long check_pairs = (long long)(check_seconds * sample_rate);
    if (check_pairs > total_pairs) check_pairs = total_pairs;

    long long clipped = 0;
    long long checked = 0;
    std::vector<int16_t> buf(4096);

    auto scan_region = [&](long long start_pair, long long num_pairs) {
        f.clear();
        f.seekg(start_pair * 4, std::ios::beg);
        long long remaining = num_pairs * 2;  // number of int16 values
        while (remaining > 0 && f.good()) {
            long long to_read = std::min(remaining, (long long)buf.size());
            f.read(reinterpret_cast<char*>(buf.data()), to_read * sizeof(int16_t));
            long long got = f.gcount() / sizeof(int16_t);
            if (got <= 0) break;
            for (long long i = 0; i < got; i++) {
                int abs_val = std::abs((int)buf[i]);
                if (abs_val > peak_abs) peak_abs = abs_val;
                if (buf[i] == 32767 || buf[i] == -32768) clipped++;
            }
            checked += got;
            remaining -= got;
        }
    };

    // Check first N seconds
    scan_region(0, check_pairs);

    // Check last N seconds (if file is long enough to not overlap)
    if (total_pairs > check_pairs * 2) {
        scan_region(total_pairs - check_pairs, check_pairs);
    }

    if (checked <= 0) return;  // keep clip_rate = -1.0
    clip_rate = (double)clipped / checked;
}

} // namespace pretty

#endif // PRETTY_AUDIO_H
