#include "doctest.h"
#include "config.h"
#include <sstream>

TEST_CASE("load_config parses key=value pairs") {
    std::istringstream input(
        "# Signal Processing\n"
        "lowpass_cutoff=4000\n"
        "bandpass_low=300\n"
        "bandpass_high=3400\n"
        "lowpass_transition=600\n"
        "bandpass_transition=150\n"
        "\n"
        "# Speech-to-Text\n"
        "model_encoder=models/sherpa-onnx/small/encoder-epoch-99-avg-1.int8.onnx\n"
        "model_decoder=models/sherpa-onnx/small/decoder-epoch-99-avg-1.onnx\n"
        "model_joiner=models/sherpa-onnx/small/joiner-epoch-99-avg-1.int8.onnx\n"
        "model_tokens=models/sherpa-onnx/small/tokens.txt\n"
        "decoding_method=modified_beam_search\n"
        "num_threads=2\n"
        "\n"
        "# Detection\n"
        "wake_word=PRETTY\n"
        "call_signs=NIGHT,DELTA,ALPHA\n"
        "command=DOOM\n"
        "fuzzy_max_distance=3\n"
    );

    PipelineConfig cfg;
    REQUIRE(load_config(input, cfg));

    CHECK(cfg.lowpass_cutoff == doctest::Approx(4000.0f));
    CHECK(cfg.bandpass_low == doctest::Approx(300.0f));
    CHECK(cfg.bandpass_high == doctest::Approx(3400.0f));
    CHECK(cfg.lowpass_transition == doctest::Approx(600.0f));
    CHECK(cfg.bandpass_transition == doctest::Approx(150.0f));
    CHECK(cfg.model_encoder == "models/sherpa-onnx/small/encoder-epoch-99-avg-1.int8.onnx");
    CHECK(cfg.model_decoder == "models/sherpa-onnx/small/decoder-epoch-99-avg-1.onnx");
    CHECK(cfg.model_joiner == "models/sherpa-onnx/small/joiner-epoch-99-avg-1.int8.onnx");
    CHECK(cfg.model_tokens == "models/sherpa-onnx/small/tokens.txt");
    CHECK(cfg.decoding_method == "modified_beam_search");
    CHECK(cfg.num_threads == 2);
    CHECK(cfg.wake_word == "PRETTY");
    CHECK(cfg.call_signs.size() == 3);
    CHECK(cfg.call_signs[0] == "NIGHT");
    CHECK(cfg.call_signs[1] == "DELTA");
    CHECK(cfg.call_signs[2] == "ALPHA");
    CHECK(cfg.commands == std::vector<std::string>{"DOOM"});
    CHECK(cfg.fuzzy_max_distance == 3);
}

TEST_CASE("load_config ignores comments and blank lines") {
    std::istringstream input(
        "# This is a comment\n"
        "\n"
        "  # Indented comment\n"
        "wake_word=TEST\n"
        "\n"
    );

    PipelineConfig cfg;
    REQUIRE(load_config(input, cfg));
    CHECK(cfg.wake_word == "TEST");
}

TEST_CASE("load_config preserves defaults for missing keys") {
    std::istringstream input("wake_word=HELLO\n");

    PipelineConfig cfg;
    REQUIRE(load_config(input, cfg));
    CHECK(cfg.wake_word == "HELLO");
    // Defaults preserved
    CHECK(cfg.lowpass_cutoff == doctest::Approx(3400.0f));
    CHECK(cfg.decoding_method == "modified_beam_search");
    CHECK(cfg.fuzzy_max_distance == 2);
}

TEST_CASE("load_config handles whitespace around =") {
    std::istringstream input("wake_word = PRETTY\n");

    PipelineConfig cfg;
    REQUIRE(load_config(input, cfg));
    CHECK(cfg.wake_word == "PRETTY");
}

TEST_CASE("load_variants parses TARGET=VARIANT1,VARIANT2") {
    std::istringstream input(
        "# Wake word variants\n"
        "PRETTY=PRETY,BRETTY,PREDDY\n"
        "\n"
        "# Command variants\n"
        "DOOM=DOM,DUM,DUME\n"
        "NIGHT=KNIGHT,NITE,NIGH\n"
    );

    VariantsMap variants;
    REQUIRE(load_variants(input, variants));

    CHECK(variants.count("PRETTY") == 1);
    CHECK(variants["PRETTY"].size() == 3);
    CHECK(variants["PRETTY"][0] == "PRETY");
    CHECK(variants["PRETTY"][1] == "BRETTY");
    CHECK(variants["PRETTY"][2] == "PREDDY");

    CHECK(variants["DOOM"].size() == 3);
    CHECK(variants["DOOM"][0] == "DOM");

    CHECK(variants["NIGHT"].size() == 3);
    CHECK(variants["NIGHT"][2] == "NIGH");
}

TEST_CASE("load_variants handles empty input") {
    std::istringstream input("");

    VariantsMap variants;
    REQUIRE(load_variants(input, variants));
    CHECK(variants.empty());
}

TEST_CASE("load_variants ignores comment-only input") {
    std::istringstream input("# Just comments\n# Nothing else\n");

    VariantsMap variants;
    REQUIRE(load_variants(input, variants));
    CHECK(variants.empty());
}
