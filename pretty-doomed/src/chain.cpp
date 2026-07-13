#include "chain.h"

#include <cmath>
#include <complex>
#include <cstdio>
#include <numeric>

#include <fftw3.h>

#include <gnuradio/filter/firdes.h>

#include "pretty_log.h"

using namespace pretty;

ChainParams compute_chain_params(const PipelineConfig& cfg, double narrow_bw_hz) {
    ChainParams p;

    if (cfg.sdr_decimation <= 0 || cfg.sdr_rate <= 0) {
        log_error() << "Invalid SDR rate or decimation\n";
        return p;
    }

    // The rate entering GNU Radio: sdr_rate (no hw FIR) or sdr_hw_fir_rate (hw FIR)
    p.gnuradio_input_rate = cfg.sdr_hw_fir_enable ? cfg.sdr_hw_fir_rate : cfg.sdr_rate;
    p.effective_rate = p.gnuradio_input_rate / cfg.sdr_decimation;
    if (p.effective_rate == 0) {
        log_error() << "Effective rate is 0\n";
        return p;
    }

    if (cfg.sdr_lpf_cutoff <= 0 || cfg.sdr_lpf_cutoff >= p.gnuradio_input_rate / 2.0) {
        log_error() << "LPF cutoff out of range\n";
        return p;
    }
    p.lpf_taps = gr::filter::firdes::low_pass(
        1.0, p.gnuradio_input_rate, cfg.sdr_lpf_cutoff, cfg.sdr_lpf_transition,
        gr::fft::window::WIN_HAMMING);
    p.bp_taps = gr::filter::firdes::band_pass(
        1.0, cfg.sdr_audio_rate, cfg.sdr_bandpass_low, cfg.sdr_bandpass_high, 200.0,
        gr::fft::window::WIN_HAMMING);

    // Optional #107 narrowing stage between the channel LPF and the
    // discriminator: band-limit to +/-narrow_bw/2 and decimate to ~25 kSPS.
    p.disc_rate = p.effective_rate;
    if (narrow_bw_hz > 0.0) {
        p.narrow_bw = narrow_bw_hz;
        p.narrow_decim = std::max(1UL, p.effective_rate / 25000);
        if (p.effective_rate % p.narrow_decim != 0) {
            log_warning() << "Narrowing: effective rate " << p.effective_rate
                          << " not divisible by decimation " << p.narrow_decim
                          << ", discriminator rate truncated\n";
        }
        p.disc_rate = p.effective_rate / p.narrow_decim;
        p.narrow_taps = gr::filter::firdes::low_pass(
            1.0, p.effective_rate, narrow_bw_hz / 2.0, narrow_bw_hz / 4.0,
            gr::fft::window::WIN_HAMMING);
    }

    unsigned long rx_gcd = std::gcd(p.disc_rate, (unsigned long)cfg.sdr_audio_rate);
    p.interpolation = cfg.sdr_audio_rate / rx_gcd;
    p.decimation = p.disc_rate / rx_gcd;

    p.valid = true;
    return p;
}

AudioChain make_audio_chain(const ChainParams& p, const PipelineConfig& cfg) {
    AudioChain c;
    c.lpf = gr::filter::fir_filter_ccf::make(cfg.sdr_decimation, p.lpf_taps);
    if (p.narrow_decim > 0) {
        c.narrow_lpf = gr::filter::fir_filter_ccf::make(p.narrow_decim, p.narrow_taps);
    }
    c.fm_demod = gr::analog::quadrature_demod_cf::make(
        p.disc_rate / (2.0 * M_PI * cfg.sdr_fm_deviation));
    c.resampler = gr::filter::rational_resampler_fff::make(p.interpolation, p.decimation);
    c.bandpass = gr::filter::fir_filter_fff::make(1, p.bp_taps);
    return c;
}

void connect_audio_chain(gr::top_block_sptr tb, const AudioChain& chain,
                         gr::blocks::head::sptr audio_head,
                         gr::blocks::wavfile_sink::sptr wav_sink) {
    gr::basic_block_sptr into_demod = chain.lpf;
    if (chain.narrow_lpf) {
        tb->connect(chain.lpf, 0, chain.narrow_lpf, 0);
        into_demod = chain.narrow_lpf;
    }
    tb->connect(into_demod, 0, chain.fm_demod, 0);
    tb->connect(chain.fm_demod, 0, chain.resampler, 0);
    tb->connect(chain.resampler, 0, chain.bandpass, 0);
    if (audio_head) {
        tb->connect(chain.bandpass, 0, audio_head, 0);
        tb->connect(audio_head, 0, wav_sink, 0);
    } else {
        tb->connect(chain.bandpass, 0, wav_sink, 0);
    }
}

void connect_audio_chain_from_baseband(gr::top_block_sptr tb, const AudioChain& chain,
                                       gr::blocks::wavfile_sink::sptr wav_sink) {
    tb->connect(chain.narrow_lpf, 0, chain.fm_demod, 0);
    tb->connect(chain.fm_demod, 0, chain.resampler, 0);
    tb->connect(chain.resampler, 0, chain.bandpass, 0);
    tb->connect(chain.bandpass, 0, wav_sink, 0);
}

double find_peak_offset(const std::string& path, double sample_rate,
                        double expect_hz, double search_hz,
                        double dc_guard_hz) {
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
        if (dc_guard_hz > 0.0 && std::abs(freq) < dc_guard_hz) continue;
        if (psd[k] > best_pow) { best_pow = psd[k]; best = freq; }
    }
    return best;
}
