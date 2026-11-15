#include "doctest.h"
#include "report_card.h"
#include "stb_image.h"

#include <cmath>
#include <cstdio>
#include <filesystem>

namespace {
constexpr int RATE = 16000;
constexpr double PI = 3.14159265358979323846;

PipelineConfig distress_config() {
    PipelineConfig cfg;
    cfg.detect_wake_word = "MAYDAY";
    cfg.detect_commands = {"SINKING", "TAKING ON WATER", "FIRE"};
    cfg.detect_fuzzy_max_distance = 1;
    return cfg;
}

std::vector<float> tone(double hz, double seconds) {
    std::vector<float> s(static_cast<std::size_t>(seconds * RATE));
    for (std::size_t i = 0; i < s.size(); i++) {
        s[i] = static_cast<float>(0.5 * std::sin(2 * PI * hz * i / RATE));
    }
    return s;
}
}  // namespace

TEST_CASE("window_matches marks the windows with an exact command phrase") {
    std::vector<std::string> windows = {"coast guard channel sixteen", "we are taking on water",
                                        "", "the boat is sinking"};
    using W = WindowMatch;
    CHECK(window_matches(windows, distress_config(), {}) ==
          std::vector<W>{W::None, W::Exact, W::None, W::Exact});
}

TEST_CASE("window_matches marks a window with only an approximate match as approximate") {
    using W = WindowMatch;
    CHECK(window_matches({"we have five people", "five people and fire"}, distress_config(), {}) ==
          std::vector<W>{W::Approximate, W::Exact});
}

TEST_CASE("window_matches does not mark a window with the wake word only") {
    CHECK(window_matches({"mayday mayday"}, distress_config(), {}) == std::vector<WindowMatch>{WindowMatch::None});
}

TEST_CASE("match_counts counts the exact matches of each phrase, highest count first") {
    DetectionResult d;
    d.command_exact_counts["SINKING"] = 1;
    d.command_exact_counts["TAKING ON WATER"] = 2;
    d.command_approx_counts["TAKING ON WATER"] = 1;
    std::vector<PhraseCount> expected = {{"TAKING ON WATER", 2, {}}, {"SINKING", 1, {}}};
    CHECK(match_counts(d).exact == expected);
}

TEST_CASE("match_counts keeps the approximate matches apart, with the words that were heard") {
    DetectionResult d;
    d.command_approx_counts["FIRE"] = 3;
    d.command_approx_matches["FIRE"] = {"FIVE", "FIVE", "HIRE"};
    std::vector<PhraseCount> expected = {{"FIRE", 3, {"FIVE", "HIRE"}}};
    CHECK(match_counts(d).approximate == expected);
    CHECK(match_counts(d).exact.empty());
}

TEST_CASE("match_counts includes the wake word when it matches") {
    DetectionResult d;
    d.wake_word_exact = 2;
    d.wake_word_approx = 1;
    d.wake_word_approx_matches = {"MAYDAI"};
    auto m = match_counts(d, "MAYDAY");
    CHECK(m.exact == std::vector<PhraseCount>{{"MAYDAY", 2, {}}});
    CHECK(m.approximate == std::vector<PhraseCount>{{"MAYDAY", 1, {"MAYDAI"}}});
}

TEST_CASE("match_counts does not count the wake word twice when it is also a command phrase") {
    DetectionResult d;
    d.wake_word_exact = 2;
    d.command_exact_counts["MAYDAY"] = 2;
    CHECK(match_counts(d, "MAYDAY").exact == std::vector<PhraseCount>{{"MAYDAY", 2, {}}});
}

TEST_CASE("match_excerpt marks the matched words and keeps the words around them") {
    DetectionResult d;
    d.command_exact_matches["TAKING ON WATER"] = {"TAKING ON WATER"};
    auto words = match_excerpt("one two three four five six seven eight nine ten we are taking on water now",
                               d, 2, 200);
    std::vector<CardWord> expected = {{"...", false}, {"we", false}, {"are", false}, {"taking", true},
                                      {"on", true},   {"water", true}, {"now", false}};
    CHECK(words == expected);
}

TEST_CASE("match_excerpt separates distant matches with an ellipsis") {
    DetectionResult d;
    d.command_exact_matches["SINKING"] = {"SINKING", "SINKING"};
    auto words = match_excerpt("sinking a b c d e f g h sinking", d, 1, 200);
    std::vector<CardWord> expected = {{"sinking", true}, {"a", false}, {"...", false},
                                      {"h", false},      {"sinking", true}};
    CHECK(words == expected);
}

TEST_CASE("match_excerpt matches words without their punctuation, and flags approximate matches") {
    DetectionResult d;
    d.command_approx_matches["FIRE"] = {"FIVE"};
    auto words = match_excerpt("we have five, people", d, 0, 200);
    CHECK(words == std::vector<CardWord>{{"...", false}, {"five,", true, true}, {"...", false}});
}

TEST_CASE("match_excerpt without a match gives the start of the transcript") {
    auto words = match_excerpt("a b c d", DetectionResult{}, 2, 5);
    CHECK(words == std::vector<CardWord>{{"a", false}, {"b", false}, {"c", false}, {"...", false}});
}

TEST_CASE("wrap_words starts a new line before a word that does not fit") {
    std::vector<CardWord> words = {{"aaa", false}, {"bbb", true}, {"cc", false}};
    auto lines = wrap_words(words, 7);
    REQUIRE(lines.size() == 2);
    CHECK(lines[0] == std::vector<CardWord>{{"aaa", false}, {"bbb", true}});
    CHECK(lines[1] == std::vector<CardWord>{{"cc", false}});
}

TEST_CASE("audio_spectrogram puts the energy of a tone in the bin of its frequency") {
    auto spec = audio_spectrogram(tone(1000.0, 2.0), RATE, 8, 512, 4000.0);
    REQUIRE(spec.size() == 8);
    REQUIRE(spec[0].size() == 128);  // 4000 Hz of 512-point bins of 31.25 Hz
    std::size_t peak = 0;
    for (std::size_t i = 1; i < spec[3].size(); i++) {
        if (spec[3][i] > spec[3][peak]) peak = i;
    }
    CHECK(peak == 32);  // 1000 Hz / 31.25 Hz
}

TEST_CASE("render_report_card writes a PNG of the card size") {
    ReportCardData data;
    data.clip = "46250-star-king";
    data.config_name = "parakeet-0.6b-v2-int8";
    data.window_s = 10.0;
    data.audio_s = 2.0;
    data.stt_s = 0.3;
    data.detected = true;
    data.window_hits = {WindowMatch::Exact};
    data.matches.exact = {{"SINKING", 1, {}}};
    data.excerpt = {{"we", false}, {"are", false}, {"sinking", true}};
    auto dir = std::filesystem::temp_directory_path() / "test_report_card";
    std::filesystem::create_directories(dir);
    std::string path = (dir / "report_card.png").string();
    std::remove(path.c_str());

    REQUIRE(render_report_card(data, tone(500.0, 2.0), RATE, path));

    int w = 0, h = 0, n = 0;
    REQUIRE(stbi_info(path.c_str(), &w, &h, &n));
    CHECK(w == REPORT_CARD_WIDTH);
    CHECK(h == REPORT_CARD_HEIGHT);
    std::filesystem::remove_all(dir);
}

TEST_CASE("render_report_card returns false when the PNG cannot be written") {
    ReportCardData data;
    CHECK_FALSE(render_report_card(data, tone(500.0, 1.0), RATE, "/nonexistent-dir/report_card.png"));
}
