#ifndef CHAIN_H
#define CHAIN_H

/**
 * chain.h - the shared audio DSP chain (#112).
 *
 * Both the flight capture (src/capture.cpp) and the ground preview tool
 * (tools/preview_onboard.cpp) build their audio path from this module, so the
 * ground preview executes the flight logic by construction: the same
 * config-derived rates and ratios, the same firdes filter designs, and the
 * same block chain
 *
 *   channel LPF -> [optional #107 narrowing LPF] -> FM discriminator
 *     -> rational resampler -> voice band-pass
 *
 * Callers keep their own front end (AD9361 device_source for flight, file
 * source for the preview) and their own sinks; capture taps its sc16 branch
 * off the channel LPF output.
 */

#include <string>
#include <vector>

#include <gnuradio/top_block.h>
#include <gnuradio/blocks/head.h>
#include <gnuradio/blocks/wavfile_sink.h>
#include <gnuradio/analog/quadrature_demod_cf.h>
#include <gnuradio/filter/rational_resampler.h>
#include <gnuradio/filter/fir_filter_blk.h>

#include "config.h"

// Rates, resampler ratios, and filter taps derived from the flight config,
// exactly as capture.cpp historically derived them. narrow_bw_hz > 0 adds the
// #107 narrowing-stage parameters; 0 keeps the chain as flown.
struct ChainParams {
    long gnuradio_input_rate = 0;      // rate entering GNU Radio (post HW-FIR)
    unsigned long effective_rate = 0;  // after the channel LPF decimation
    unsigned long disc_rate = 0;       // rate entering the FM discriminator
    unsigned long interpolation = 0;   // audio resampler ratios
    unsigned long decimation = 0;
    std::vector<float> lpf_taps;
    std::vector<float> bp_taps;
    double narrow_bw = 0.0;            // total width in Hz; 0 = disabled
    unsigned long narrow_decim = 0;    // 0 = disabled
    std::vector<float> narrow_taps;
    bool valid = false;
};

ChainParams compute_chain_params(const PipelineConfig& cfg, double narrow_bw_hz = 0.0);

// The audio-branch blocks. Upstream complex samples connect into `lpf`.
struct AudioChain {
    gr::filter::fir_filter_ccf::sptr lpf;
    gr::filter::fir_filter_ccf::sptr narrow_lpf;   // null unless narrowing
    gr::analog::quadrature_demod_cf::sptr fm_demod;
    gr::filter::rational_resampler_fff::sptr resampler;
    gr::filter::fir_filter_fff::sptr bandpass;
};

AudioChain make_audio_chain(const ChainParams& p, const PipelineConfig& cfg);

// Wire lpf -> [narrow_lpf] -> fm_demod -> resampler -> bandpass
// [-> audio_head] -> wav_sink. audio_head may be null (the preview tool runs
// its file to completion instead of counting samples).
void connect_audio_chain(gr::top_block_sptr tb, const AudioChain& chain,
                         gr::blocks::head::sptr audio_head,
                         gr::blocks::wavfile_sink::sptr wav_sink);

// Averaged-PSD peak search in a raw interleaved-int16 I/Q file, within
// +/-search_hz of expect_hz (Hann-windowed 8192-point FFTs). Returns the
// strongest bin's offset in Hz relative to the recording center, or expect_hz
// if the file cannot be read.
double find_peak_offset(const std::string& path, double sample_rate,
                        double expect_hz, double search_hz);

#endif
