#include "doctest.h"
#include "dsp.h"
#include <cmath>

TEST_CASE("apply_lowpass does not crash on valid input") {
    // 1 second of silence at 48 kHz
    std::vector<float> input(48000, 0.0f);
    auto out = apply_lowpass(input, 48000.0f, 3400.0f, 500.0f);
    CHECK(out.size() > 0);
}

TEST_CASE("apply_lowpass preserves DC signal") {
    // Constant signal should pass through lowpass unchanged
    std::vector<float> input(48000, 0.5f);
    auto out = apply_lowpass(input, 48000.0f, 3400.0f, 500.0f);
    REQUIRE(out.size() > 0);
    // Check steady-state (skip transient at start)
    size_t steady = out.size() / 2;
    CHECK(out[steady] == doctest::Approx(0.5f).epsilon(0.05));
}

TEST_CASE("apply_bandpass does not crash on valid input") {
    std::vector<float> input(48000, 0.0f);
    auto out = apply_bandpass(input, 48000.0f, 300.0f, 3400.0f, 100.0f);
    CHECK(out.size() > 0);
}

TEST_CASE("apply_bandpass attenuates DC signal") {
    // DC (0 Hz) is outside the bandpass (300-3400 Hz), so it should be attenuated
    std::vector<float> input(48000, 1.0f);
    auto out = apply_bandpass(input, 48000.0f, 300.0f, 3400.0f, 100.0f);
    REQUIRE(out.size() > 0);
    size_t steady = out.size() / 2;
    CHECK(std::abs(out[steady]) < 0.1f);  // DC should be heavily attenuated
}

TEST_CASE("resample same rate returns same size") {
    std::vector<float> input = {1.0f, 2.0f, 3.0f, 4.0f};
    auto out = resample(input, 16000, 16000);

    REQUIRE(out.size() == input.size());
    CHECK(out[0] == doctest::Approx(1.0f));
    CHECK(out[3] == doctest::Approx(4.0f));
}

TEST_CASE("resample 2:1 downsampling") {
    std::vector<float> input(1000, 1.0f);
    auto out = resample(input, 32000, 16000);
    CHECK(out.size() == 500);
}

TEST_CASE("resample 1:2 upsampling") {
    std::vector<float> input(500, 1.0f);
    auto out = resample(input, 16000, 32000);
    CHECK(out.size() == 1000);
}

TEST_CASE("resample 48000 to 16000") {
    std::vector<float> input(4800, 0.5f);
    auto out = resample(input, 48000, 16000);
    CHECK(out.size() == 1600);
    CHECK(out[0] == doctest::Approx(0.5f));
}

TEST_CASE("resample preserves constant signal") {
    std::vector<float> input(100, 0.75f);
    auto out = resample(input, 44100, 16000);

    for (size_t i = 0; i < out.size(); i++) {
        CHECK(out[i] == doctest::Approx(0.75f));
    }
}

TEST_CASE("resample empty input") {
    std::vector<float> input;
    auto out = resample(input, 48000, 16000);
    CHECK(out.empty());
}
