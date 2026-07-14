#include "doctest.h"
#include "pretty_resample.h"

using pretty::resample;

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
