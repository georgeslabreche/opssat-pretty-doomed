#include "doctest.h"
#include "output.h"

TEST_CASE("compute_totals empty detection") {
    DetectionResult d;
    auto t = compute_totals(d);
    CHECK(t.command_exact == 0);
    CHECK(t.command_approx == 0);
    CHECK(t.points_exact == 0);
    CHECK(t.points_approx == 0);
    CHECK(t.points == 0);
    CHECK(t.command_detected == false);
}

TEST_CASE("compute_totals exact matches only") {
    DetectionResult d;
    d.wake_word_exact = 2;
    d.command_exact_counts["DOOM"] = 3;
    d.command_exact_counts["PLAY DOOM"] = 2;

    auto t = compute_totals(d);
    CHECK(t.command_exact == 5);
    CHECK(t.command_approx == 0);
    CHECK(t.points_exact == 14);  // (2 wake + 5 command) * 2
    CHECK(t.points_approx == 0);
    CHECK(t.points == 14);
    CHECK(t.command_detected == true);
}

TEST_CASE("compute_totals approximate matches only") {
    DetectionResult d;
    d.wake_word_approx = 1;
    d.command_approx_counts["DOOM"] = 2;

    auto t = compute_totals(d);
    CHECK(t.command_exact == 0);
    CHECK(t.command_approx == 2);
    CHECK(t.points_exact == 0);
    CHECK(t.points_approx == 3);  // 1 wake + 2 command
    CHECK(t.points == 3);
    CHECK(t.command_detected == true);
}

TEST_CASE("compute_totals mixed exact and approximate") {
    DetectionResult d;
    d.wake_word_exact = 1;
    d.wake_word_approx = 1;
    d.command_exact_counts["DOOM"] = 2;
    d.command_approx_counts["DOOM"] = 3;

    auto t = compute_totals(d);
    CHECK(t.command_exact == 2);
    CHECK(t.command_approx == 3);
    CHECK(t.points_exact == 6);   // (1 wake + 2 command) * 2
    CHECK(t.points_approx == 4);  // (1 wake + 3 command) * 1
    CHECK(t.points == 10);
    CHECK(t.command_detected == true);
}

TEST_CASE("compute_totals wake word only - no command") {
    DetectionResult d;
    d.wake_word_exact = 3;

    auto t = compute_totals(d);
    CHECK(t.command_detected == false);
    CHECK(t.points == 6);  // 3 exact * 2
}

TEST_CASE("format_scores basic output") {
    DetectionResult d;
    d.wake_word_exact = 1;
    d.wake_word_exact_matches = {"PRETTY"};
    d.wake_word_approx = 0;
    d.command_exact_counts["DOOM"] = 1;
    d.command_exact_matches["DOOM"] = {"DOOM"};

    DetectionTotals t = compute_totals(d);
    std::string scores = format_scores(d, t);

    CHECK(scores.find("wake_word_exact=1\n") != std::string::npos);
    CHECK(scores.find("wake_word_exact_matches=PRETTY\n") != std::string::npos);
    CHECK(scores.find("wake_word_approx=0\n") != std::string::npos);
    CHECK(scores.find("wake_word_approx_matches=\n") != std::string::npos);
    CHECK(scores.find("command_DOOM_exact=1\n") != std::string::npos);
    CHECK(scores.find("command_DOOM_exact_matches=DOOM\n") != std::string::npos);
    CHECK(scores.find("command_DOOM_approx=0\n") != std::string::npos);
    CHECK(scores.find("command_DOOM_approx_matches=\n") != std::string::npos);
    CHECK(scores.find("total_points=4\n") != std::string::npos);    // (1 wake + 1 cmd) * 2
    CHECK(scores.find("total_points_exact=4\n") != std::string::npos);
    CHECK(scores.find("total_points_approx=0\n") != std::string::npos);
}

TEST_CASE("format_scores multi-word command key") {
    DetectionResult d;
    d.command_exact_counts["PLAY DOOM"] = 1;
    d.command_exact_matches["PLAY DOOM"] = {"PLAY DOOM"};

    DetectionTotals t = compute_totals(d);
    std::string scores = format_scores(d, t);

    // Spaces in command name become underscores in key
    CHECK(scores.find("command_PLAY_DOOM_exact=1\n") != std::string::npos);
}

TEST_CASE("format_scores with call signs") {
    DetectionResult d;
    d.call_sign_counts["NIGHT"] = 2;
    d.call_sign_matches["NIGHT"] = {"NIGHT", "KNIGHT"};

    DetectionTotals t = compute_totals(d);
    std::string scores = format_scores(d, t);

    CHECK(scores.find("call_sign_NIGHT=2\n") != std::string::npos);
    CHECK(scores.find("call_sign_NIGHT_matches=NIGHT,KNIGHT\n") != std::string::npos);
}

TEST_CASE("format_scores multiple comma-separated matches") {
    DetectionResult d;
    d.wake_word_exact = 2;
    d.wake_word_exact_matches = {"PRETTY", "PRETTY"};
    d.command_approx_counts["DOOM"] = 3;
    d.command_approx_matches["DOOM"] = {"DO", "DO", "DO"};

    DetectionTotals t = compute_totals(d);
    std::string scores = format_scores(d, t);

    CHECK(scores.find("wake_word_exact_matches=PRETTY,PRETTY\n") != std::string::npos);
    CHECK(scores.find("command_DOOM_approx_matches=DO,DO,DO\n") != std::string::npos);
}

TEST_CASE("format_summary command detected") {
    PipelineConfig cfg;
    cfg.detect_wake_word = "PRETTY";

    DetectionResult d;
    d.wake_word_exact = 1;
    d.wake_word_exact_matches = {"PRETTY"};
    d.command_exact_counts["DOOM"] = 1;
    d.command_exact_matches["DOOM"] = {"DOOM"};

    DetectionTotals t = compute_totals(d);
    std::string summary = format_summary(d, t, cfg, "input/test.wav", "PRETTY PLAY DOOM");

    CHECK(summary.find("=== PRETTY DOOMed Summary ===") != std::string::npos);
    CHECK(summary.find("Input: input/test.wav") != std::string::npos);
    CHECK(summary.find("Transcription: PRETTY PLAY DOOM") != std::string::npos);
    CHECK(summary.find("Wake word (PRETTY):") != std::string::npos);
    CHECK(summary.find("Exact: 1 [PRETTY]") != std::string::npos);
    CHECK(summary.find("Command [DOOM]:") != std::string::npos);
    CHECK(summary.find("Total points: 4 (exact=4, approx=0)") != std::string::npos);
    CHECK(summary.find("Result: COMMAND DETECTED - launching DOOM") != std::string::npos);
}

TEST_CASE("format_summary no command") {
    PipelineConfig cfg;
    cfg.detect_wake_word = "PRETTY";

    DetectionResult d;
    d.wake_word_exact = 1;
    d.wake_word_exact_matches = {"PRETTY"};

    DetectionTotals t = compute_totals(d);
    std::string summary = format_summary(d, t, cfg, "input/test.wav", "PRETTY HELLO WORLD");

    CHECK(summary.find("Result: No command detected") != std::string::npos);
}

TEST_CASE("format_summary with call signs") {
    PipelineConfig cfg;
    cfg.detect_wake_word = "PRETTY";

    DetectionResult d;
    d.wake_word_exact = 1;
    d.wake_word_exact_matches = {"PRETTY"};
    d.call_sign_counts["NIGHT"] = 1;
    d.call_sign_matches["NIGHT"] = {"NIGHT"};

    DetectionTotals t = compute_totals(d);
    std::string summary = format_summary(d, t, cfg, "input/test.wav", "PRETTY NIGHT PLAY");

    CHECK(summary.find("Call sign [NIGHT]: 1") != std::string::npos);
    CHECK(summary.find("Matches: NIGHT") != std::string::npos);
}
