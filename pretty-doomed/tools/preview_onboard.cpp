/**
 * preview_onboard.cpp - Offline preview of the on-board doom audio pipeline.
 *
 * Feeds a downlinked raw sc16 I/Q file through the SAME GNU Radio DSP chain the
 * flight app runs in capture.cpp (complex low-pass + decimation -> quadrature
 * FM demod -> rational resampler -> voice band-pass -> WAV, then RMS-normalize
 * to -20 dBFS), so we can hear what the on-board pipeline would have produced
 * from a pass that was instead downlinked as raw I/Q.
 *
 * capture.cpp is hardwired to the AD9361 device_source, so it cannot be called
 * directly on a file. The audio chain is built from the same shared module
 * capture.cpp uses (src/chain.{h,cpp}, #112) and driven from the same flight
 * config, so preview and flight execute the same DSP by construction; only the
 * front end differs, and it EMULATES the SDR hardware the recording bypassed:
 *   - LO tuning. The recording is centered at 1295.5 MHz; the flight config
 *     tunes to 1296.0 MHz. We rotate the spectrum by that 0.5 MHz difference so
 *     the uplink lands where the on-board receiver would place it (near DC),
 *     inside the 85 kHz channel low-pass.
 *   - AD9361 decimating HW FIR. The recording is raw 2.5 MSPS; the flight path
 *     feeds GNU Radio at 600 kSPS post-FIR. We rational-resample to that rate.
 * Everything after the front end is the flight chain, unmodified.
 *
 * Optional narrowing stage (issue #107): with a non-zero [narrow_bw_hz] the
 * tool inserts, between the channel low-pass and the FM discriminator:
 *   - a peak search within +/-100 kHz of the expected uplink (tracks Doppler
 *     and transmitter offset), used to tune the found peak to DC,
 *   - a complex low-pass to +/-narrow_bw/2 with decimation to ~25 kSPS.
 * The discriminator then sees only the signal bandwidth instead of the full
 * ~170 kHz baseband (roughly 9-11 dB less noise power). narrow_bw_hz = 0
 * (default) keeps the flight chain exactly as flown.
 *
 * Usage: preview_onboard <in.sc16> <out.wav> [config.cfg] [rec_center_hz]
 *                        [rec_rate_hz] [narrow_bw_hz]
 */
#include <cmath>
#include <cstdio>
#include <numeric>
#include <string>
#include <vector>

#include <gnuradio/top_block.h>
#include <gnuradio/blocks/file_source.h>
#include <gnuradio/blocks/interleaved_short_to_complex.h>
#include <gnuradio/blocks/rotator_cc.h>
#include <gnuradio/blocks/wavfile_sink.h>
#include <gnuradio/filter/rational_resampler.h>

#include "chain.h"
#include "config.h"
#include "pretty_audio.h"
#include "pretty_log.h"

using namespace pretty;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
            "usage: preview_onboard <in.sc16> <out.wav> [config.cfg] "
            "[rec_center_hz] [rec_rate_hz] [narrow_bw_hz]\n");
        return 1;
    }
    std::string in_path = argv[1];
    std::string out_path = argv[2];
    std::string cfg_path = argc > 3 ? argv[3] : "config.cfg";
    double rec_center = 1295500000.0;
    double rec_rate = 2500000.0;
    double narrow_bw = 0.0;   // 0 = flight chain as flown
    try {
        if (argc > 4) rec_center = std::stod(argv[4]);
        if (argc > 5) rec_rate = std::stod(argv[5]);
        if (argc > 6) narrow_bw = std::stod(argv[6]);
    } catch (const std::exception&) {
        std::fprintf(stderr, "error: rec_center_hz, rec_rate_hz, and narrow_bw_hz "
                             "must be numeric\n");
        return 1;
    }

    PipelineConfig cfg;
    if (!load_config(cfg_path, cfg)) {
        log_error() << "failed to load config " << cfg_path << "\n";
        return 1;
    }

    // ---- Shared audio-chain parameters (#112): the same module capture.cpp
    // uses, so preview and flight execute the same chain by construction ----
    ChainParams params = compute_chain_params(cfg, narrow_bw);
    if (!params.valid) {
        return 1;
    }

    // ---- Front-end emulation of the SDR hardware the recording bypassed ----
    double f_shift = (double)cfg.sdr_frequency - rec_center;         // e.g. +500 kHz

    // ---- Optional narrowing stage (issue #107) ----
    // Tune the *found* peak (not the nominal uplink) to DC; the band-limiting
    // and decimation live in the shared chain.
    bool narrow = params.narrow_decim > 0;
    if (narrow) {
        double f_peak = find_peak_offset(in_path, rec_rate, f_shift, 100e3);
        log_info() << "Narrowing: peak found at " << (f_peak - f_shift) / 1e3
                   << " kHz from nominal uplink (offset " << f_peak / 1e3
                   << " kHz in recording)\n";
        f_shift = f_peak;
    }

    double phase_inc = -2.0 * M_PI * f_shift / rec_rate;             // rotate uplink to DC
    unsigned long fe_gcd = std::gcd((unsigned long)rec_rate, (unsigned long)params.gnuradio_input_rate);
    unsigned long fe_interp = params.gnuradio_input_rate / fe_gcd;
    unsigned long fe_decim = (unsigned long)rec_rate / fe_gcd;

    log_info() << "Preview of the on-board pipeline\n";
    log_info() << "  input:   " << in_path << " (center " << rec_center / 1e6
               << " MHz, " << rec_rate / 1e6 << " MSPS)\n";
    log_info() << "  LO tune: " << f_shift / 1e3 << " kHz shift (uplink "
               << cfg.sdr_frequency / 1e6 << " MHz -> DC); front resample "
               << (long)rec_rate << " -> " << params.gnuradio_input_rate
               << " (" << fe_interp << "/" << fe_decim << ")\n";
    log_info() << "  chain:   LPF cutoff " << cfg.sdr_lpf_cutoff << " Hz, decim "
               << cfg.sdr_decimation << " -> " << params.effective_rate << " Hz; FM dev "
               << cfg.sdr_fm_deviation << "; resample -> " << cfg.sdr_audio_rate
               << " (" << params.interpolation << "/" << params.decimation << "); bandpass "
               << cfg.sdr_bandpass_low << "-" << cfg.sdr_bandpass_high << " Hz\n";
    if (narrow) {
        log_info() << "  narrow:  +/-" << params.narrow_bw / 2e3 << " kHz low-pass, decim "
                   << params.narrow_decim << " -> " << params.disc_rate
                   << " Hz into the discriminator (" << params.narrow_taps.size() << " taps)\n";
    }
    log_info() << "  taps:    LPF " << params.lpf_taps.size() << ", bandpass " << params.bp_taps.size() << "\n";

    auto tb = gr::make_top_block("preview_onboard");

    auto src = gr::blocks::file_source::make(sizeof(short), in_path.c_str(), false);
    auto s2c = gr::blocks::interleaved_short_to_complex::make();
    auto rot = gr::blocks::rotator_cc::make(phase_inc);
    auto fe_rs = gr::filter::rational_resampler_ccf::make(fe_interp, fe_decim);

    // Flight chain: the shared module used by capture.cpp (#112), including
    // the optional narrowing stage between the channel LPF and discriminator.
    AudioChain chain = make_audio_chain(params, cfg);
    auto wav_sink = gr::blocks::wavfile_sink::make(
        out_path.c_str(), 1, cfg.sdr_audio_rate,
        gr::blocks::FORMAT_WAV, gr::blocks::FORMAT_PCM_16);

    tb->connect(src, 0, s2c, 0);
    tb->connect(s2c, 0, rot, 0);
    tb->connect(rot, 0, fe_rs, 0);
    tb->connect(fe_rs, 0, chain.lpf, 0);
    connect_audio_chain(tb, chain, nullptr, wav_sink);

    log_info() << "Running...\n";
    tb->run();
    tb.reset();  // flush and close the WAV

    if (!rms_normalize(out_path, -20.0)) {
        log_warning() << "audio normalization failed\n";
    }
    log_info() << "wrote " << out_path << "\n";
    return 0;
}
