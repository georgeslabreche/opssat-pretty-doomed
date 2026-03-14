#ifndef LOOPBACK_QUALITY_H
#define LOOPBACK_QUALITY_H

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include <sndfile.h>
#include "pretty_log.h"

using namespace pretty;

// Loopback signal quality check: mean-centered normalized cross-correlation
// between input and output WAV over a fixed window (first 3 seconds).
// Resamples input to output rate via linear interpolation if rates differ.
// Uses max(|corr|) to handle polarity flips from the demod chain.
// Returns peak |correlation| (0.0 = uncorrelated, 1.0 = identical).
// Sets best_lag_ms to the alignment offset in milliseconds.
inline double loopback_quality_check(const std::string& input_wav, const std::string& output_wav,
                              double& best_lag_ms) {
    best_lag_ms = 0.0;
    const double WINDOW_SEC = 3.0;  // fixed window to keep O(N*lags) bounded on ARM

    // Read input WAV
    SF_INFO in_info = {0};
    SNDFILE* in_sf = sf_open(input_wav.c_str(), SFM_READ, &in_info);
    if (!in_sf) {
        log_warning() << "Quality check: could not open input WAV: " << sf_strerror(NULL) << "\n";
        return -1.0;
    }
    std::vector<float> in_samples(in_info.frames);
    sf_count_t in_read = sf_readf_float(in_sf, in_samples.data(), in_info.frames);
    sf_close(in_sf);
    if (in_read <= 0) {
        log_warning() << "Quality check: no frames read from input WAV\n";
        return -1.0;
    }
    in_samples.resize((size_t)in_read);

    // Read output WAV
    SF_INFO out_info = {0};
    SNDFILE* out_sf = sf_open(output_wav.c_str(), SFM_READ, &out_info);
    if (!out_sf) {
        log_warning() << "Quality check: could not open output WAV: " << sf_strerror(NULL) << "\n";
        return -1.0;
    }
    std::vector<float> out_samples(out_info.frames);
    sf_count_t out_read = sf_readf_float(out_sf, out_samples.data(), out_info.frames);
    sf_close(out_sf);
    if (out_read <= 0) {
        log_warning() << "Quality check: no frames read from output WAV\n";
        return -1.0;
    }
    out_samples.resize((size_t)out_read);

    if (out_info.samplerate <= 0) {
        log_warning() << "Quality check: invalid output sample rate\n";
        return -1.0;
    }

    // Resample input to output rate if different (linear interpolation)
    std::vector<float> resampled;
    if (in_info.samplerate != out_info.samplerate && in_info.samplerate > 0) {
        double ratio = (double)out_info.samplerate / in_info.samplerate;
        size_t new_len = (size_t)(in_samples.size() * ratio);
        if (new_len == 0) {
            log_warning() << "Quality check: resampled length is 0\n";
            return -1.0;
        }
        resampled.resize(new_len);
        for (size_t i = 0; i < new_len; i++) {
            double src_idx = i / ratio;
            size_t idx0 = (size_t)src_idx;
            double frac = src_idx - idx0;
            if (idx0 + 1 < in_samples.size()) {
                resampled[i] = (float)((1.0 - frac) * in_samples[idx0] + frac * in_samples[idx0 + 1]);
            } else if (idx0 < in_samples.size()) {
                resampled[i] = in_samples[idx0];
            }
        }
        log_info() << "Quality check: resampled input " << in_info.samplerate
                   << " -> " << out_info.samplerate << " Hz (" << resampled.size() << " samples)\n";
    }

    // Use resampled input if rate conversion was needed, otherwise original
    const std::vector<float>& ref = resampled.empty() ? in_samples : resampled;

    // Cap to fixed window (first WINDOW_SEC seconds) to bound CPU on constrained targets
    size_t window_samples = (out_info.samplerate > 0)
        ? (size_t)(WINDOW_SEC * out_info.samplerate) : ref.size();

    // Search window: ±0.1 seconds at output rate (handles FM demod group delay)
    int max_lag = out_info.samplerate / 10;
    // Cap max_lag to 1/4 of shortest input to preserve useful overlap
    {
        size_t shortest = std::min({ref.size(), out_samples.size(), window_samples});
        if (max_lag > (int)shortest / 4) max_lag = (int)shortest / 4;
    }
    if (max_lag <= 0) return -1.0;

    // Compute overlap_len directly from both vectors and lag window.
    // Worst-case out index: out_start_max + overlap_len - 1 = 2*max_lag + overlap_len - 1.
    // Require: 2*max_lag + overlap_len <= out_samples.size()
    // Also:    overlap_len <= ref.size()
    // Also:    overlap_len <= window_samples
    if (out_samples.size() <= (size_t)(2 * max_lag)) return -1.0;
    size_t overlap_len = std::min({
        out_samples.size() - (size_t)(2 * max_lag),
        ref.size(),
        window_samples
    });
    if (overlap_len < 100) {
        log_warning() << "Quality check: overlap too short (" << overlap_len << " samples) for correlation\n";
        return -1.0;
    }
    log_info() << "Quality check: overlap=" << overlap_len << " samples ("
               << (double)overlap_len / out_info.samplerate << "s), max_lag=±"
               << max_lag << "\n";

    // Mean-center reference signal over the overlap window to remove DC offset
    // (FM demod can introduce DC bias). Reference window is fixed at [0..overlap_len).
    double ref_mean = 0.0;
    for (size_t i = 0; i < overlap_len; i++) {
        ref_mean += ref[i];
    }
    ref_mean /= overlap_len;

    // Compute energy of mean-centered reference (constant across all lags)
    double ref_energy = 0.0;
    for (size_t i = 0; i < overlap_len; i++) {
        double v = ref[i] - ref_mean;
        ref_energy += v * v;
    }
    if (ref_energy < 1e-20) {
        log_warning() << "Quality check: reference signal is silent after DC removal\n";
        return -1.0;
    }

    double best_corr = 0.0;
    int best_lag = 0;

    for (int lag = -max_lag; lag <= max_lag; lag++) {
        // For each lag, correlate ref[0..overlap_len) with out[out_start..out_start+overlap_len)
        int out_start = max_lag + lag;

        // Compute output mean over the actual correlated window (not a fixed offset)
        double out_mean = 0.0;
        for (size_t i = 0; i < overlap_len; i++) {
            out_mean += out_samples[out_start + (int)i];
        }
        out_mean /= overlap_len;

        double sum = 0.0;
        double out_energy = 0.0;
        for (size_t i = 0; i < overlap_len; i++) {
            double r = ref[i] - ref_mean;
            double o = out_samples[out_start + (int)i] - out_mean;
            sum += r * o;
            out_energy += o * o;
        }

        if (out_energy < 1e-20) continue;
        double corr = sum / std::sqrt(ref_energy * out_energy);
        // Use |corr| to handle polarity flips from demod chain
        if (std::abs(corr) > std::abs(best_corr)) {
            best_corr = corr;
            best_lag = lag;
        }
    }

    best_lag_ms = (out_info.samplerate > 0)
        ? (double)best_lag * 1000.0 / out_info.samplerate : 0.0;

    return std::abs(best_corr);
}

#endif // LOOPBACK_QUALITY_H
