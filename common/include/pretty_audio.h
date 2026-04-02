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

// Diagnostic statistics for sc16 I/Q files.
// Computes per-channel RMS, DC offset, min/max, and zero fraction
// to identify dead signal (all zeros), noise, clipping, or DC bias.
struct Sc16Stats {
    long long samples = 0;          // complex samples analyzed
    double rms_i = 0, rms_q = 0;    // RMS per channel (int16 scale)
    double mean_i = 0, mean_q = 0;  // DC offset per channel
    int16_t min_i = 0, max_i = 0;   // range I
    int16_t min_q = 0, max_q = 0;   // range Q
    long long zero_i = 0, zero_q = 0; // count of exact-zero samples
    int peak_i = 0, peak_q = 0;     // peak absolute value per channel
    double papr_i = 0, papr_q = 0;  // peak-to-average power ratio (dB)
    double imbalance_db = 0;         // I/Q imbalance: 20*log10(rms_i/rms_q)
    bool valid = false;
};

inline Sc16Stats analyze_sc16(const std::string& path, long long max_samples = 0) {
    Sc16Stats s;
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) return s;

    long long file_bytes = (long long)f.tellg();
    if (file_bytes < 4) return s;

    long long total_samples = file_bytes / 4;  // 2 int16 per complex sample
    long long to_analyze = total_samples;
    if (max_samples > 0 && to_analyze > max_samples)
        to_analyze = max_samples;

    f.seekg(0, std::ios::beg);

    double sum_i = 0, sum_q = 0;
    double sumsq_i = 0, sumsq_q = 0;
    s.min_i = 32767; s.max_i = -32768;
    s.min_q = 32767; s.max_q = -32768;

    std::vector<int16_t> buf(8192);  // pairs of I,Q
    long long remaining = to_analyze;
    while (remaining > 0 && f.good()) {
        // Read pairs (each pair = 2 int16 = 4 bytes)
        long long pairs_to_read = std::min(remaining, (long long)(buf.size() / 2));
        f.read(reinterpret_cast<char*>(buf.data()), pairs_to_read * 4);
        long long got_bytes = f.gcount();
        long long got_pairs = got_bytes / 4;
        if (got_pairs <= 0) break;

        for (long long j = 0; j < got_pairs; j++) {
            int16_t vi = buf[j * 2];
            int16_t vq = buf[j * 2 + 1];
            sum_i += vi; sum_q += vq;
            sumsq_i += (double)vi * vi;
            sumsq_q += (double)vq * vq;
            if (vi < s.min_i) s.min_i = vi;
            if (vi > s.max_i) s.max_i = vi;
            if (vq < s.min_q) s.min_q = vq;
            if (vq > s.max_q) s.max_q = vq;
            if (vi == 0) s.zero_i++;
            if (vq == 0) s.zero_q++;
        }
        s.samples += got_pairs;
        remaining -= got_pairs;
    }

    if (s.samples > 0) {
        s.mean_i = sum_i / s.samples;
        s.mean_q = sum_q / s.samples;
        s.rms_i = std::sqrt(sumsq_i / s.samples);
        s.rms_q = std::sqrt(sumsq_q / s.samples);
        s.peak_i = std::max(std::abs((int)s.min_i), std::abs((int)s.max_i));
        s.peak_q = std::max(std::abs((int)s.min_q), std::abs((int)s.max_q));
        if (s.rms_i > 0) s.papr_i = 20.0 * std::log10((double)s.peak_i / s.rms_i);
        if (s.rms_q > 0) s.papr_q = 20.0 * std::log10((double)s.peak_q / s.rms_q);
        if (s.rms_i > 0 && s.rms_q > 0)
            s.imbalance_db = 20.0 * std::log10(s.rms_i / s.rms_q);
        s.valid = true;
    }
    return s;
}

// Derive metrics CSV path from sc16 path: /dir/capture.sc16 -> /dir/capture-metrics.csv
inline std::string make_metrics_filename(const std::string& sc16_path) {
    auto sep = sc16_path.find_last_of("/\\");
    if (sep != std::string::npos)
        return sc16_path.substr(0, sep + 1) + "capture-metrics.csv";
    return "capture-metrics.csv";
}

// Write I/Q diagnostic metrics to CSV. Single header + data row.
inline bool write_sc16_metrics(const std::string& path,
                               const Sc16Stats& s,
                               float iq_scale) {
    if (!s.valid) return false;
    std::ofstream f(path);
    if (!f) return false;

    double rms_i_db = (s.rms_i > 0) ? 20.0 * std::log10(s.rms_i / iq_scale) : -999.0;
    double rms_q_db = (s.rms_q > 0) ? 20.0 * std::log10(s.rms_q / iq_scale) : -999.0;
    double zero_pct_i = 100.0 * s.zero_i / s.samples;
    double zero_pct_q = 100.0 * s.zero_q / s.samples;

    f << "samples,rms_i,rms_q,rms_i_dbfs,rms_q_dbfs,"
      << "peak_i,peak_q,papr_i_db,papr_q_db,"
      << "dc_offset_i,dc_offset_q,imbalance_db,"
      << "zero_pct_i,zero_pct_q\n";
    f << s.samples << ","
      << s.rms_i << "," << s.rms_q << ","
      << rms_i_db << "," << rms_q_db << ","
      << s.peak_i << "," << s.peak_q << ","
      << s.papr_i << "," << s.papr_q << ","
      << s.mean_i << "," << s.mean_q << "," << s.imbalance_db << ","
      << zero_pct_i << "," << zero_pct_q << "\n";
    return f.good();
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
