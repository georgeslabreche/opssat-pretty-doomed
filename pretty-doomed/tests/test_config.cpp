#include "doctest.h"
#include "config.h"
#include <sstream>

TEST_CASE("load_config parses key=value pairs") {
    std::istringstream input(        "\n"
        "# Speech-to-Text\n"
        "stt_model_encoder=models/sherpa-onnx/small/encoder-epoch-99-avg-1.int8.onnx\n"
        "stt_model_decoder=models/sherpa-onnx/small/decoder-epoch-99-avg-1.onnx\n"
        "stt_model_joiner=models/sherpa-onnx/small/joiner-epoch-99-avg-1.int8.onnx\n"
        "stt_model_tokens=models/sherpa-onnx/small/tokens.txt\n"
        "stt_decoding_method=modified_beam_search\n"
        "stt_num_threads=2\n"
        "\n"
        "# Detection\n"
        "detect_wake_word=PRETTY\n"
        "detect_call_signs=NIGHT,DELTA,ALPHA\n"
        "detect_command=DOOM\n"
        "detect_fuzzy_max_distance=3\n"
        "\n"
        "# DOOM Frame Capture\n"
        "doom_frames_e1m7-607=5000,5001-5020\n"
        "doom_frames_impfight=700,701-720\n"
        "doom_keepgifframes=true\n"
    );

    PipelineConfig cfg;
    REQUIRE(load_config(input, cfg));

    CHECK(cfg.stt_model_encoder == "models/sherpa-onnx/small/encoder-epoch-99-avg-1.int8.onnx");
    CHECK(cfg.stt_model_decoder == "models/sherpa-onnx/small/decoder-epoch-99-avg-1.onnx");
    CHECK(cfg.stt_model_joiner == "models/sherpa-onnx/small/joiner-epoch-99-avg-1.int8.onnx");
    CHECK(cfg.stt_model_tokens == "models/sherpa-onnx/small/tokens.txt");
    CHECK(cfg.stt_decoding_method == "modified_beam_search");
    CHECK(cfg.stt_num_threads == 2);
    CHECK(cfg.detect_wake_word == "PRETTY");
    CHECK(cfg.detect_call_signs.size() == 3);
    CHECK(cfg.detect_call_signs[0] == "NIGHT");
    CHECK(cfg.detect_call_signs[1] == "DELTA");
    CHECK(cfg.detect_call_signs[2] == "ALPHA");
    CHECK(cfg.detect_commands == std::vector<std::string>{"DOOM"});
    CHECK(cfg.detect_fuzzy_max_distance == 3);
    CHECK(cfg.doom_frames.size() == 2);
    CHECK(cfg.doom_frames.at("e1m7-607") == "5000,5001-5020");
    CHECK(cfg.doom_frames.at("impfight") == "700,701-720");
    CHECK(cfg.doom_keepgifframes == true);
}

TEST_CASE("load_config ignores comments and blank lines") {
    std::istringstream input(
        "# This is a comment\n"
        "\n"
        "  # Indented comment\n"
        "detect_wake_word=TEST\n"
        "\n"
    );

    PipelineConfig cfg;
    REQUIRE(load_config(input, cfg));
    CHECK(cfg.detect_wake_word == "TEST");
}

TEST_CASE("load_config preserves defaults for missing keys") {
    std::istringstream input("detect_wake_word=HELLO\n");

    PipelineConfig cfg;
    REQUIRE(load_config(input, cfg));
    CHECK(cfg.detect_wake_word == "HELLO");
    // Defaults preserved
    CHECK(cfg.stt_decoding_method == "modified_beam_search");
    CHECK(cfg.detect_fuzzy_max_distance == 2);
    CHECK(cfg.doom_frames.empty());
    CHECK(cfg.doom_keepgifframes == false);
}

TEST_CASE("load_config handles whitespace around =") {
    std::istringstream input("detect_wake_word = PRETTY\n");

    PipelineConfig cfg;
    REQUIRE(load_config(input, cfg));
    CHECK(cfg.detect_wake_word == "PRETTY");
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

TEST_CASE("load_config parses hardware FIR settings") {
    std::istringstream input(
        "sdr_hw_fir_enable=true\n"
        "sdr_hw_fir_rate=521000\n"
        "sdr_hw_fir_fpass=90000\n"
        "sdr_hw_fir_fstop=110000\n"
        "sdr_hw_fir_wnom_tx=200000\n"
        "sdr_hw_fir_wnom_rx=200000\n"
    );

    PipelineConfig cfg;
    REQUIRE(load_config(input, cfg));

    CHECK(cfg.sdr_hw_fir_enable == true);
    CHECK(cfg.sdr_hw_fir_rate == 521000);
    CHECK(cfg.sdr_hw_fir_fpass == 90000);
    CHECK(cfg.sdr_hw_fir_fstop == 110000);
    CHECK(cfg.sdr_hw_fir_wnom_tx == 200000);
    CHECK(cfg.sdr_hw_fir_wnom_rx == 200000);
}

TEST_CASE("load_config preserves hw_fir defaults when not set") {
    std::istringstream input("sdr_rate=2400000\n");

    PipelineConfig cfg;
    REQUIRE(load_config(input, cfg));
    CHECK(cfg.sdr_hw_fir_enable == false);
    CHECK(cfg.sdr_hw_fir_rate == 0);
    CHECK(cfg.sdr_hw_fir_fpass == 0);
    CHECK(cfg.sdr_hw_fir_fstop == 0);
    CHECK(cfg.sdr_hw_fir_wnom_tx == 0);
    CHECK(cfg.sdr_hw_fir_wnom_rx == 0);
}

TEST_CASE("load_config parses sdr_init_per_capture") {
    std::istringstream input("sdr_init_per_capture=true\n");

    PipelineConfig cfg;
    REQUIRE(load_config(input, cfg));
    CHECK(cfg.sdr_init_per_capture == true);
}

TEST_CASE("load_config preserves sdr_init_per_capture default") {
    std::istringstream input("sdr_rate=2400000\n");

    PipelineConfig cfg;
    REQUIRE(load_config(input, cfg));
    CHECK(cfg.sdr_init_per_capture == false);
}

TEST_CASE("load_config parses sdr_enable_psd and sdr_keep_sc16") {
    std::istringstream input("sdr_enable_psd=false\nsdr_keep_sc16=true\n");

    PipelineConfig cfg;
    REQUIRE(load_config(input, cfg));
    CHECK(cfg.sdr_enable_psd == false);
    CHECK(cfg.sdr_keep_sc16 == true);
}

TEST_CASE("load_config preserves sdr_enable_psd and sdr_keep_sc16 defaults") {
    std::istringstream input("sdr_rate=2400000\n");

    PipelineConfig cfg;
    REQUIRE(load_config(input, cfg));
    CHECK(cfg.sdr_enable_psd == true);
    CHECK(cfg.sdr_keep_sc16 == false);
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

TEST_CASE("load_config parses narrowing settings (#111)") {
    std::istringstream input(
        "sdr_narrow_enable=true\n"
        "sdr_narrow_bw=20000\n"
        "sdr_narrow_search=100000\n");
    PipelineConfig cfg;
    REQUIRE(load_config(input, cfg));
    CHECK(cfg.sdr_narrow_enable == true);
    CHECK(cfg.sdr_narrow_bw == doctest::Approx(20000.0));
    CHECK(cfg.sdr_narrow_search == doctest::Approx(100000.0));
}

TEST_CASE("narrowing defaults keep the chain as flown") {
    std::istringstream input("sdr_frequency=1296000000\n");
    PipelineConfig cfg;
    REQUIRE(load_config(input, cfg));
    CHECK(cfg.sdr_narrow_enable == false);   // missing key = behavior as flown
    CHECK(cfg.sdr_narrow_bw == doctest::Approx(20000.0));
    CHECK(cfg.sdr_narrow_search == doctest::Approx(100000.0));
}
