#include "dsp.h"
#include <gnuradio/filter/firdes.h>
#include <gnuradio/top_block.h>
#include <gnuradio/blocks/vector_source.h>
#include <gnuradio/blocks/vector_sink.h>
#include <gnuradio/filter/fir_filter_blk.h>

static std::vector<float> apply_fir(const std::vector<float>& input,
                                     const std::vector<float>& taps) {
    if (input.empty() || taps.empty()) return {};

    auto tb = gr::make_top_block("fir");
    auto src = gr::blocks::vector_source_f::make(input, false);
    auto filt = gr::filter::fir_filter_fff::make(1, taps);
    auto sink = gr::blocks::vector_sink_f::make();

    tb->connect(src, 0, filt, 0);
    tb->connect(filt, 0, sink, 0);
    tb->run();

    return sink->data();
}

std::vector<float> apply_lowpass(const std::vector<float>& input,
                                  float sample_rate, float cutoff, float transition) {
    auto taps = gr::filter::firdes::low_pass(
        1.0, sample_rate, cutoff, transition,
        gr::fft::window::WIN_HAMMING
    );
    return apply_fir(input, taps);
}

std::vector<float> apply_bandpass(const std::vector<float>& input,
                                   float sample_rate, float low, float high, float transition) {
    auto taps = gr::filter::firdes::band_pass(
        1.0, sample_rate, low, high, transition,
        gr::fft::window::WIN_HAMMING
    );
    return apply_fir(input, taps);
}

std::vector<float> resample(const std::vector<float>& input,
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
