#include "doctest.h"
#include "matcher.h"

TEST_CASE("to_upper") {
    CHECK(to_upper("hello") == "HELLO");
    CHECK(to_upper("Hello World") == "HELLO WORLD");
    CHECK(to_upper("ALREADY") == "ALREADY");
    CHECK(to_upper("") == "");
    CHECK(to_upper("123abc") == "123ABC");
}

TEST_CASE("split_words") {
    CHECK(split_words("PRETTY PLAY DOOM") == std::vector<std::string>{"PRETTY", "PLAY", "DOOM"});
    CHECK(split_words("  PRETTY   PLAY  ") == std::vector<std::string>{"PRETTY", "PLAY"});
    CHECK(split_words("SINGLE") == std::vector<std::string>{"SINGLE"});
    CHECK(split_words("") == std::vector<std::string>{});
    CHECK(split_words("   ") == std::vector<std::string>{});
}

TEST_CASE("levenshtein distance") {
    SUBCASE("identical strings") {
        CHECK(levenshtein("DOOM", "DOOM") == 0);
        CHECK(levenshtein("", "") == 0);
    }

    SUBCASE("single edit") {
        CHECK(levenshtein("DOOM", "DOM") == 1);      // deletion
        CHECK(levenshtein("DOOM", "DOOOM") == 1);     // insertion
        CHECK(levenshtein("DOOM", "BOOM") == 1);      // substitution
    }

    SUBCASE("multiple edits") {
        CHECK(levenshtein("PRETTY", "PRETY") == 1);
        CHECK(levenshtein("PRETTY", "BRETTY") == 1);
        CHECK(levenshtein("OPSSAT", "UPSET") == 3);
    }

    SUBCASE("empty vs non-empty") {
        CHECK(levenshtein("ABC", "") == 3);
        CHECK(levenshtein("", "ABC") == 3);
    }
}

TEST_CASE("fuzzy_match exact target") {
    std::vector<std::string> variants;
    CHECK(fuzzy_match("DOOM", "DOOM", variants, 2) == MatchType::EXACT);
    CHECK(fuzzy_match("HELLO", "DOOM", variants, 2) == MatchType::NONE);
}

TEST_CASE("fuzzy_match with variants") {
    std::vector<std::string> variants = {"PRETY", "BRETTY", "PREDDY"};

    CHECK(fuzzy_match("PRETTY", "PRETTY", variants, 2) == MatchType::EXACT);   // exact target
    CHECK(fuzzy_match("PRETY", "PRETTY", variants, 2) == MatchType::EXACT);    // exact variant
    CHECK(fuzzy_match("BRETTY", "PRETTY", variants, 2) == MatchType::EXACT);   // exact variant
    CHECK(fuzzy_match("HELLO", "PRETTY", variants, 2) == MatchType::NONE);
}

TEST_CASE("fuzzy_match within edit distance") {
    std::vector<std::string> variants;

    CHECK(fuzzy_match("DOM", "DOOM", variants, 1) == MatchType::APPROXIMATE);     // distance 1 from target
    CHECK(fuzzy_match("DUM", "DOOM", variants, 1) == MatchType::NONE);            // distance 2, too far
    CHECK(fuzzy_match("DUM", "DOOM", variants, 2) == MatchType::APPROXIMATE);     // distance 2, within limit
    CHECK(fuzzy_match("DUNE", "DOOM", variants, 2) == MatchType::NONE);           // distance 3, too far
    CHECK(fuzzy_match("DUNE", "DOOM", variants, 3) == MatchType::APPROXIMATE);    // distance 3, within limit
}

TEST_CASE("fuzzy_match within edit distance of variant") {
    std::vector<std::string> variants = {"DOM"};

    CHECK(fuzzy_match("DO", "DOOM", variants, 1) == MatchType::APPROXIMATE); // distance 1 from variant "DOM"
}

TEST_CASE("detect full transcript") {
    PipelineConfig cfg;
    cfg.wake_word = "PRETTY";
    cfg.call_signs = {"NIGHT", "DELTA"};
    cfg.commands = {"DOOM"};
    cfg.fuzzy_max_distance = 2;

    VariantsMap variants;
    variants["PRETTY"] = {"PRETY", "BRETTY"};
    variants["DOOM"] = {"DOM", "DUM"};
    variants["NIGHT"] = {"KNIGHT", "NITE"};

    SUBCASE("exact match all components") {
        auto r = detect("PRETTY NIGHT PLAY DOOM", cfg, variants);
        CHECK(r.wake_word_exact == 1);
        CHECK(r.wake_word_approx == 0);
        CHECK(r.command_exact_counts.at("DOOM") == 1);
        CHECK(r.call_sign_counts.at("NIGHT") == 1);
        CHECK(r.wake_word_exact_matches == std::vector<std::string>{"PRETTY"});
        CHECK(r.command_exact_matches.at("DOOM") == std::vector<std::string>{"DOOM"});
        CHECK(r.call_sign_matches.at("NIGHT") == std::vector<std::string>{"NIGHT"});
    }

    SUBCASE("variant match is exact") {
        auto r = detect("PRETY KNIGHT PLAY DOM", cfg, variants);
        CHECK(r.wake_word_exact == 1);  // PRETY is an exact variant match
        CHECK(r.wake_word_approx == 0);
        CHECK(r.command_exact_counts.at("DOOM") == 1);  // DOM is an exact variant match
        CHECK(r.call_sign_counts.at("NIGHT") == 1);
        CHECK(r.wake_word_exact_matches == std::vector<std::string>{"PRETY"});
        CHECK(r.command_exact_matches.at("DOOM") == std::vector<std::string>{"DOM"});
        CHECK(r.call_sign_matches.at("NIGHT") == std::vector<std::string>{"KNIGHT"});
    }

    SUBCASE("approximate match") {
        // "PRETTX" is not PRETTY or any variant, but Levenshtein("PRETTX","PRETTY")=1 <= 2
        auto r = detect("PRETTX NIGHT PLAY DOAM", cfg, variants);
        CHECK(r.wake_word_exact == 0);
        CHECK(r.wake_word_approx == 1);
        CHECK(r.wake_word_approx_matches == std::vector<std::string>{"PRETTX"});
        CHECK(r.command_exact_counts.empty());
        CHECK(r.command_approx_counts.at("DOOM") == 1);
        CHECK(r.command_approx_matches.at("DOOM") == std::vector<std::string>{"DOAM"});
    }

    SUBCASE("no wake word") {
        auto r = detect("NIGHT PLAY DOOM", cfg, variants);
        CHECK(r.wake_word_exact == 0);
        CHECK(r.wake_word_approx == 0);
        CHECK(r.command_exact_counts.at("DOOM") == 1);
    }

    SUBCASE("no command") {
        auto r = detect("PRETTY NIGHT PLAY SOMETHING", cfg, variants);
        CHECK(r.wake_word_exact == 1);
        CHECK(r.command_exact_counts.empty());
        CHECK(r.command_approx_counts.empty());
    }

    SUBCASE("repeated commands") {
        auto r = detect("PRETTY NIGHT PLAY DOOM PRETTY DELTA PLAY DOOM", cfg, variants);
        CHECK(r.wake_word_exact == 2);
        CHECK(r.command_exact_counts.at("DOOM") == 2);
        CHECK(r.call_sign_counts.at("NIGHT") == 1);
        CHECK(r.call_sign_counts.at("DELTA") == 1);
        CHECK(r.wake_word_exact_matches == std::vector<std::string>{"PRETTY", "PRETTY"});
        CHECK(r.command_exact_matches.at("DOOM") == std::vector<std::string>{"DOOM", "DOOM"});
    }

    SUBCASE("transcript with noise words") {
        auto r = detect("AND PRETTY THE NIGHT PLEASE PLAY DOOM THEN", cfg, variants);
        CHECK(r.wake_word_exact == 1);
        CHECK(r.command_exact_counts.at("DOOM") == 1);
        CHECK(r.call_sign_counts.at("NIGHT") == 1);
    }

    SUBCASE("case insensitive") {
        auto r = detect("pretty night play doom", cfg, variants);
        CHECK(r.wake_word_exact == 1);
        CHECK(r.command_exact_counts.at("DOOM") == 1);
    }

    SUBCASE("empty transcript") {
        auto r = detect("", cfg, variants);
        CHECK(r.wake_word_exact == 0);
        CHECK(r.wake_word_approx == 0);
        CHECK(r.command_exact_counts.empty());
        CHECK(r.command_approx_counts.empty());
        CHECK(r.call_sign_counts.empty());
    }

    SUBCASE("no call signs configured") {
        PipelineConfig cfg2;
        cfg2.wake_word = "PRETTY";
        cfg2.commands = {"DOOM"};
        cfg2.fuzzy_max_distance = 2;

        auto r = detect("PRETTY PLAY DOOM", cfg2, variants);
        CHECK(r.wake_word_exact == 1);
        CHECK(r.command_exact_counts.at("DOOM") == 1);
        CHECK(r.call_sign_counts.empty());
    }

    SUBCASE("multi-word command pattern") {
        PipelineConfig cfg2;
        cfg2.wake_word = "PRETTY";
        cfg2.call_signs = {"NIGHT", "DELTA"};
        cfg2.commands = {"DOOM", "PLAY DOOM"};
        cfg2.fuzzy_max_distance = 2;

        auto r = detect("PRETTY NIGHT PLAY DOOM", cfg2, variants);
        CHECK(r.wake_word_exact == 1);
        CHECK(r.command_exact_counts.at("DOOM") == 1);
        CHECK(r.command_exact_counts.at("PLAY DOOM") == 1);
        CHECK(r.command_exact_matches.at("DOOM") == std::vector<std::string>{"DOOM"});
        CHECK(r.command_exact_matches.at("PLAY DOOM") == std::vector<std::string>{"PLAY DOOM"});
    }

    SUBCASE("multi-word command approximate match") {
        PipelineConfig cfg2;
        cfg2.wake_word = "PRETTY";
        cfg2.commands = {"DOOM", "PLAY DOOM"};
        cfg2.fuzzy_max_distance = 2;

        // "PLAX" is approximate for "PLAY", "DOOM" is exact
        // Multi-word match: one approx word makes entire phrase approximate
        auto r = detect("PRETTY PLAX DOOM", cfg2, variants);
        CHECK(r.command_exact_counts.at("DOOM") == 1);  // single-word DOOM is exact
        CHECK(r.command_approx_counts.at("PLAY DOOM") == 1);  // phrase is approx
        CHECK(r.command_approx_matches.at("PLAY DOOM") == std::vector<std::string>{"PLAX DOOM"});
    }

    SUBCASE("multi-word command no phrase match") {
        PipelineConfig cfg2;
        cfg2.wake_word = "PRETTY";
        cfg2.commands = {"DOOM", "PLAY DOOM"};
        cfg2.fuzzy_max_distance = 2;

        // "DOOM" alone matches single-word, but no "PLAY DOOM" phrase
        auto r = detect("PRETTY NIGHT DOOM", cfg2, variants);
        CHECK(r.command_exact_counts.at("DOOM") == 1);
        CHECK(r.command_exact_counts.count("PLAY DOOM") == 0);
        CHECK(r.command_approx_counts.count("PLAY DOOM") == 0);
    }
}
