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
 * directly on a file. The blocks and filter designs below are copied verbatim
 * from capture.cpp and driven from the same flight config; only the front end
 * differs, and it EMULATES the SDR hardware the raw recording bypassed:
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
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <complex>
#include <numeric>
#include <string>
#include <vector>

#include <fftw3.h>

#include <gnuradio/top_block.h>
#include <gnuradio/blocks/file_source.h>
#include <gnuradio/blocks/interleaved_short_to_complex.h>
#include <gnuradio/blocks/rotator_cc.h>
#include <gnuradio/blocks/wavfile_sink.h>
#include <gnuradio/analog/quadrature_demod_cf.h>
#include <gnuradio/filter/rational_resampler.h>
#include <gnuradio/filter/fir_filter_blk.h>
#include <gnuradio/filter/firdes.h>

#include "config.h"
#include "pretty_audio.h"
#include "pretty_log.h"

using namespace pretty;

// Averaged-PSD peak search in the raw sc16, within +/-search_hz of expect_hz.
// Returns the offset (Hz, relative to the recording center) of the strongest
// bin, or expect_hz if the file cannot be read.
static double find_peak_offset(const std::string& path, double sample_rate,
                               double expect_hz, double search_hz) {
    const int N = 8192;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return expect_hz;

    std::vector<short> raw(2 * N);
    std::vector<std::complex<float>> in(N), out(N);
    std::vector<double> psd(N, 0.0);
    fftwf_plan plan = fftwf_plan_dft_1d(
        N, reinterpret_cast<fftwf_complex*>(in.data()),
        reinterpret_cast<fftwf_complex*>(out.data()), FFTW_FORWARD, FFTW_ESTIMATE);

    int frames = 0;
    while (std::fread(raw.data(), sizeof(short), 2 * N, f) == (size_t)(2 * N)) {
        for (int i = 0; i < N; i++) {
            // Hann window against leakage from the DC spike and strong spurs
            float w = 0.5f - 0.5f * std::cos(2.0f * (float)M_PI * i / (N - 1));
            in[i] = std::complex<float>(raw[2 * i] * w, raw[2 * i + 1] * w);
        }
        fftwf_execute(plan);
        for (int i = 0; i < N; i++) psd[i] += std::norm(out[i]);
        frames++;
    }
    fftwf_destroy_plan(plan);
    std::fclose(f);
    if (frames == 0) return expect_hz;

    double best = expect_hz, best_pow = -1.0;
    for (int k = 0; k < N; k++) {
        double freq = (k <= N / 2 ? k : k - N) * sample_rate / N;
        if (std::abs(freq - expect_hz) > search_hz) continue;
        if (psd[k] > best_pow) { best_pow = psd[k]; best = freq; }
    }
    return best;
}

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

    // ---- Flight parameters, derived exactly as capture.cpp does ----
    long gnuradio_input_rate = cfg.sdr_hw_fir_enable ? cfg.sdr_hw_fir_rate : cfg.sdr_rate;
    unsigned long effective_rate = gnuradio_input_rate / cfg.sdr_decimation;

    std::vector<float> lpf_taps = gr::filter::firdes::low_pass(
        1.0, gnuradio_input_rate, cfg.sdr_lpf_cutoff, cfg.sdr_lpf_transition,
        gr::fft::window::WIN_HAMMING);
    std::vector<float> bp_taps = gr::filter::firdes::band_pass(
        1.0, cfg.sdr_audio_rate, cfg.sdr_bandpass_low, cfg.sdr_bandpass_high, 200.0,
        gr::fft::window::WIN_HAMMING);

    // ---- Front-end emulation of the SDR hardware the recording bypassed ----
    double f_shift = (double)cfg.sdr_frequency - rec_center;         // e.g. +500 kHz

    // ---- Optional narrowing stage (issue #107) ----
    // Tune the *found* peak (not the nominal uplink) to DC, then band-limit to
    // +/-narrow_bw/2 and decimate before the discriminator.
    bool narrow = narrow_bw > 0.0;
    unsigned long narrow_decim = 1;
    unsigned long disc_rate = effective_rate;    // rate entering the discriminator
    std::vector<float> narrow_taps;
    if (narrow) {
        double f_peak = find_peak_offset(in_path, rec_rate, f_shift, 100e3);
        log_info() << "Narrowing: peak found at " << (f_peak - f_shift) / 1e3
                   << " kHz from nominal uplink (offset " << f_peak / 1e3
                   << " kHz in recording)\n";
        f_shift = f_peak;
        narrow_decim = std::max(1UL, effective_rate / 25000);   // ~25 kSPS at the discriminator
        if (effective_rate % narrow_decim != 0) {
            log_warning() << "Narrowing: effective rate " << effective_rate
                          << " not divisible by decimation " << narrow_decim
                          << ", discriminator rate truncated\n";
        }
        disc_rate = effective_rate / narrow_decim;
        narrow_taps = gr::filter::firdes::low_pass(
            1.0, effective_rate, narrow_bw / 2.0, narrow_bw / 4.0,
            gr::fft::window::WIN_HAMMING);
    }

    unsigned long rx_gcd = std::gcd(disc_rate, (unsigned long)cfg.sdr_audio_rate);
    unsigned long interpolation = cfg.sdr_audio_rate / rx_gcd;
    unsigned long decimation = disc_rate / rx_gcd;

    double phase_inc = -2.0 * M_PI * f_shift / rec_rate;             // rotate uplink to DC
    unsigned long fe_gcd = std::gcd((unsigned long)rec_rate, (unsigned long)gnuradio_input_rate);
    unsigned long fe_interp = gnuradio_input_rate / fe_gcd;
    unsigned long fe_decim = (unsigned long)rec_rate / fe_gcd;

    log_info() << "Preview of the on-board pipeline\n";
    log_info() << "  input:   " << in_path << " (center " << rec_center / 1e6
               << " MHz, " << rec_rate / 1e6 << " MSPS)\n";
    log_info() << "  LO tune: " << f_shift / 1e3 << " kHz shift (uplink "
               << cfg.sdr_frequency / 1e6 << " MHz -> DC); front resample "
               << (long)rec_rate << " -> " << gnuradio_input_rate
               << " (" << fe_interp << "/" << fe_decim << ")\n";
    log_info() << "  chain:   LPF cutoff " << cfg.sdr_lpf_cutoff << " Hz, decim "
               << cfg.sdr_decimation << " -> " << effective_rate << " Hz; FM dev "
               << cfg.sdr_fm_deviation << "; resample -> " << cfg.sdr_audio_rate
               << " (" << interpolation << "/" << decimation << "); bandpass "
               << cfg.sdr_bandpass_low << "-" << cfg.sdr_bandpass_high << " Hz\n";
    if (narrow) {
        log_info() << "  narrow:  +/-" << narrow_bw / 2e3 << " kHz low-pass, decim "
                   << narrow_decim << " -> " << disc_rate
                   << " Hz into the discriminator (" << narrow_taps.size() << " taps)\n";
    }
    log_info() << "  taps:    LPF " << lpf_taps.size() << ", bandpass " << bp_taps.size() << "\n";

    auto tb = gr::make_top_block("preview_onboard");

    auto src = gr::blocks::file_source::make(sizeof(short), in_path.c_str(), false);
    auto s2c = gr::blocks::interleaved_short_to_complex::make();
    auto rot = gr::blocks::rotator_cc::make(phase_inc);
    auto fe_rs = gr::filter::rational_resampler_ccf::make(fe_interp, fe_decim);

    // Flight chain (verbatim from capture.cpp branch 2), with the optional
    // narrowing stage inserted between the channel LPF and the discriminator.
    auto lpf = gr::filter::fir_filter_ccf::make(cfg.sdr_decimation, lpf_taps);
    auto fm_demod = gr::analog::quadrature_demod_cf::make(
        disc_rate / (2.0 * M_PI * cfg.sdr_fm_deviation));
    auto resampler = gr::filter::rational_resampler_fff::make(interpolation, decimation);
    auto bandpass = gr::filter::fir_filter_fff::make(1, bp_taps);
    auto wav_sink = gr::blocks::wavfile_sink::make(
        out_path.c_str(), 1, cfg.sdr_audio_rate,
        gr::blocks::FORMAT_WAV, gr::blocks::FORMAT_PCM_16);

    tb->connect(src, 0, s2c, 0);
    tb->connect(s2c, 0, rot, 0);
    tb->connect(rot, 0, fe_rs, 0);
    tb->connect(fe_rs, 0, lpf, 0);
    if (narrow) {
        auto narrow_lpf = gr::filter::fir_filter_ccf::make(narrow_decim, narrow_taps);
        tb->connect(lpf, 0, narrow_lpf, 0);
        tb->connect(narrow_lpf, 0, fm_demod, 0);
    } else {
        tb->connect(lpf, 0, fm_demod, 0);
    }
    tb->connect(fm_demod, 0, resampler, 0);
    tb->connect(resampler, 0, bandpass, 0);
    tb->connect(bandpass, 0, wav_sink, 0);

    log_info() << "Running...\n";
    tb->run();
    tb.reset();  // flush and close the WAV

    if (!rms_normalize(out_path, -20.0)) {
        log_warning() << "audio normalization failed\n";
    }
    log_info() << "wrote " << out_path << "\n";
    return 0;
}
