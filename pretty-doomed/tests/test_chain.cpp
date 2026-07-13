#include "doctest.h"
#include "chain.h"

#include <cmath>
#include <cstdio>
#include <vector>

// The expected values in these tests are characterization values: they pin the
// shared chain (#112) to what capture.cpp and preview_onboard historically
// computed and logged with the flight config (LPF 97 taps, bandpass 193 taps,
// resampler 2/25; narrowing validated in #107 with 97 taps, decim 8, 16/25).

static PipelineConfig flight_config() {
    PipelineConfig cfg;
    cfg.sdr_rate = 2400000;
    cfg.sdr_decimation = 3;
    cfg.sdr_hw_fir_enable = true;
    cfg.sdr_hw_fir_rate = 600000;
    cfg.sdr_hw_fir_fpass = 200000;
    cfg.sdr_hw_fir_fstop = 250000;
    cfg.sdr_hw_fir_wnom_tx = 400000;
    cfg.sdr_hw_fir_wnom_rx = 350000;
    cfg.sdr_audio_rate = 16000;
    cfg.sdr_lpf_cutoff = 85000.0;
    cfg.sdr_lpf_transition = 15000.0;
    cfg.sdr_bandpass_low = 300.0;
    cfg.sdr_bandpass_high = 3400.0;
    cfg.sdr_fm_deviation = 5000.0;
    return cfg;
}

TEST_CASE("chain params match the flight configuration as historically derived") {
    ChainParams p = compute_chain_params(flight_config());
    REQUIRE(p.valid);
    CHECK(p.gnuradio_input_rate == 600000);
    CHECK(p.effective_rate == 200000);
    CHECK(p.disc_rate == 200000);          // no narrowing: discriminator at effective rate
    CHECK(p.interpolation == 2);           // 200000 -> 16000
    CHECK(p.decimation == 25);
    CHECK(p.lpf_taps.size() == 97);
    CHECK(p.bp_taps.size() == 193);
    CHECK(p.narrow_decim == 0);
    CHECK(p.narrow_taps.empty());
}

TEST_CASE("chain params with the validated #107 narrowing settings") {
    ChainParams p = compute_chain_params(flight_config(), 20000.0);
    REQUIRE(p.valid);
    CHECK(p.effective_rate == 200000);
    CHECK(p.narrow_decim == 8);            // 200000 -> 25000 at the discriminator
    CHECK(p.disc_rate == 25000);
    CHECK(p.interpolation == 16);          // 25000 -> 16000
    CHECK(p.decimation == 25);
    CHECK(p.narrow_taps.size() == 97);
    CHECK(p.lpf_taps.size() == 97);        // channel filter unchanged by narrowing
}

TEST_CASE("chain params without the hardware FIR") {
    PipelineConfig cfg = flight_config();
    cfg.sdr_hw_fir_enable = false;
    cfg.sdr_decimation = 12;               // 2.4 MSPS / 12 = 200 kHz, the pre-HW-FIR config
    ChainParams p = compute_chain_params(cfg);
    REQUIRE(p.valid);
    CHECK(p.gnuradio_input_rate == 2400000);
    CHECK(p.effective_rate == 200000);
    CHECK(p.interpolation == 2);
    CHECK(p.decimation == 25);
}

TEST_CASE("chain params reject invalid configurations") {
    PipelineConfig cfg = flight_config();
    cfg.sdr_decimation = 0;
    CHECK_FALSE(compute_chain_params(cfg).valid);

    cfg = flight_config();
    cfg.sdr_lpf_cutoff = 400000.0;         // >= Nyquist of the 600 kSPS input
    CHECK_FALSE(compute_chain_params(cfg).valid);

    cfg = flight_config();
    cfg.sdr_hw_fir_rate = 0;               // hw FIR enabled but rate missing
    CHECK_FALSE(compute_chain_params(cfg).valid);
}

TEST_CASE("make_audio_chain creates the narrowing stage only when configured") {
    PipelineConfig cfg = flight_config();

    ChainParams wide = compute_chain_params(cfg);
    AudioChain c1 = make_audio_chain(wide, cfg);
    CHECK(c1.lpf);
    CHECK_FALSE(c1.narrow_lpf);
    CHECK(c1.fm_demod);
    CHECK(c1.resampler);
    CHECK(c1.bandpass);

    ChainParams narrow = compute_chain_params(cfg, 20000.0);
    AudioChain c2 = make_audio_chain(narrow, cfg);
    CHECK(c2.narrow_lpf);
}

// ---- find_peak_offset: synthesized sc16 with a tone at a known offset ----

static std::string write_tone_sc16(double sample_rate, double tone_hz, int n_samples) {
    std::string path = "/tmp/test_chain_tone.sc16";
    FILE* f = std::fopen(path.c_str(), "wb");
    REQUIRE(f != nullptr);
    for (int i = 0; i < n_samples; i++) {
        double ph = 2.0 * M_PI * tone_hz * i / sample_rate;
        short iq[2] = { (short)(8000 * std::cos(ph)), (short)(8000 * std::sin(ph)) };
        std::fwrite(iq, sizeof(short), 2, f);
    }
    std::fclose(f);
    return path;
}

TEST_CASE("find_peak_offset locates a positive-offset tone within one FFT bin") {
    const double fs = 2500000.0, tone = 491900.0;
    std::string path = write_tone_sc16(fs, tone, 8192 * 20);
    double found = find_peak_offset(path, fs, 500000.0, 100e3);
    CHECK(std::abs(found - tone) <= fs / 8192.0);   // one bin, ~305 Hz
    std::remove(path.c_str());
}

TEST_CASE("find_peak_offset locates a negative-offset tone") {
    const double fs = 2500000.0, tone = -491900.0;
    std::string path = write_tone_sc16(fs, tone, 8192 * 20);
    double found = find_peak_offset(path, fs, -500000.0, 100e3);
    CHECK(std::abs(found - tone) <= fs / 8192.0);
    std::remove(path.c_str());
}

TEST_CASE("find_peak_offset ignores a tone outside the search window") {
    const double fs = 2500000.0;
    std::string path = write_tone_sc16(fs, 491900.0, 8192 * 20);
    // Search around -500 kHz: the +492 kHz tone is out of the +/-100 kHz window,
    // so the expected offset comes back (only noise-free zeros elsewhere).
    double found = find_peak_offset(path, fs, -500000.0, 100e3);
    CHECK(std::abs(found - (-500000.0)) <= 100e3);
    std::remove(path.c_str());
}

TEST_CASE("find_peak_offset returns the expected offset for a missing file") {
    CHECK(find_peak_offset("/nonexistent/file.sc16", 2500000.0, 123.0, 100e3)
          == doctest::Approx(123.0));
}
