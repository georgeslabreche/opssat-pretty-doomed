#ifndef DSP_H
#define DSP_H

#include <vector>

// Design and apply lowpass FIR filter using GNU Radio.
std::vector<float> apply_lowpass(const std::vector<float>& input,
                                  float sample_rate, float cutoff, float transition);

// Design and apply bandpass FIR filter using GNU Radio.
std::vector<float> apply_bandpass(const std::vector<float>& input,
                                   float sample_rate, float low, float high, float transition);

// Resample audio using linear interpolation.
std::vector<float> resample(const std::vector<float>& input,
                             int input_rate, int output_rate);

#endif
