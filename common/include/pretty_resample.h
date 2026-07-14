/*
 * pretty_resample.h - Linear-interpolation audio resampler for OPS-SAT PRETTY
 *
 * Header-only, no external dependencies.
 */
#ifndef PRETTY_RESAMPLE_H
#define PRETTY_RESAMPLE_H

#include <vector>

namespace pretty {

// Resample audio using linear interpolation.
inline std::vector<float> resample(const std::vector<float>& input,
                                   int input_rate, int output_rate) {
    if (input.empty()) return {};
    if (input_rate == output_rate) return input;

    double ratio = static_cast<double>(output_rate) / input_rate;
    int output_frames = static_cast<int>(input.size() * ratio);
    std::vector<float> output(output_frames);

    for (int i = 0; i < output_frames; i++) {
        double src_idx = i / ratio;
        int idx0 = static_cast<int>(src_idx);
        int idx1 = idx0 + 1;
        double frac = src_idx - idx0;

        if (idx1 >= static_cast<int>(input.size())) {
            idx1 = input.size() - 1;
        }

        output[i] = input[idx0] * (1.0 - frac) + input[idx1] * frac;
    }

    return output;
}

}  // namespace pretty

#endif
