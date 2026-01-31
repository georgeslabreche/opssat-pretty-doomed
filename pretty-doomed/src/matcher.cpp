#include "matcher.h"
#include <algorithm>
#include <sstream>
#include <vector>
#include <cctype>

std::string to_upper(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    return result;
}

std::vector<std::string> split_words(const std::string& text) {
    std::vector<std::string> words;
    std::istringstream stream(text);
    std::string word;
    while (stream >> word) {
        words.push_back(word);
    }
    return words;
}

int levenshtein(const std::string& a, const std::string& b) {
    int m = a.size();
    int n = b.size();

    std::vector<std::vector<int>> dp(m + 1, std::vector<int>(n + 1, 0));

    for (int i = 0; i <= m; i++) dp[i][0] = i;
    for (int j = 0; j <= n; j++) dp[0][j] = j;

    for (int i = 1; i <= m; i++) {
        for (int j = 1; j <= n; j++) {
            int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            dp[i][j] = std::min({
                dp[i - 1][j] + 1,      // deletion
                dp[i][j - 1] + 1,      // insertion
                dp[i - 1][j - 1] + cost // substitution
            });
        }
    }

    return dp[m][n];
}

MatchType fuzzy_match(const std::string& candidate,
                      const std::string& target,
                      const std::vector<std::string>& variants,
                      int max_distance) {
    // Check exact match against target
    if (candidate == target) return MatchType::EXACT;

    // Check exact match against variants
    for (const auto& variant : variants) {
        if (candidate == variant) return MatchType::EXACT;
    }

    // Check approximate match (require first letter to match)
    if (!candidate.empty() && !target.empty() && candidate[0] == target[0]) {
        if (levenshtein(candidate, target) <= max_distance) return MatchType::APPROXIMATE;
    }

    for (const auto& variant : variants) {
        if (!candidate.empty() && !variant.empty() && candidate[0] == variant[0]) {
            if (levenshtein(candidate, variant) <= max_distance) return MatchType::APPROXIMATE;
        }
    }

    return MatchType::NONE;
}

static std::vector<std::string> get_variants(const std::string& key,
                                              const VariantsMap& variants) {
    auto it = variants.find(key);
    if (it != variants.end()) return it->second;
    return {};
}

DetectionResult detect(const std::string& transcript,
                       const PipelineConfig& cfg,
                       const VariantsMap& variants) {
    DetectionResult result;

    std::string upper_transcript = to_upper(transcript);
    std::vector<std::string> words = split_words(upper_transcript);

    std::string wake_upper = to_upper(cfg.wake_word);

    // Prepare command patterns (single-word and multi-word)
    struct CmdPattern {
        std::string name;                       // original config string, e.g. "PLAY DOOM"
        std::vector<std::string> pattern_words; // e.g. {"PLAY", "DOOM"}
        std::vector<std::string> variants_list;
    };
    std::vector<CmdPattern> cmd_patterns;
    for (const auto& cmd : cfg.commands) {
        CmdPattern cp;
        cp.name = to_upper(cmd);
        cp.pattern_words = split_words(cp.name);
        if (cp.pattern_words.size() == 1) {
            cp.variants_list = get_variants(cp.pattern_words[0], variants);
        }
        cmd_patterns.push_back(std::move(cp));
    }

    auto wake_variants = get_variants(wake_upper, variants);

    for (size_t i = 0; i < words.size(); i++) {
        const auto& word = words[i];

        // Check wake word
        MatchType wake_mt = fuzzy_match(word, wake_upper, wake_variants, cfg.fuzzy_max_distance);
        if (wake_mt == MatchType::EXACT) {
            result.wake_word_exact++;
            result.wake_word_exact_matches.push_back(word);
        } else if (wake_mt == MatchType::APPROXIMATE) {
            result.wake_word_approx++;
            result.wake_word_approx_matches.push_back(word);
        }

        // Check command patterns
        for (const auto& cp : cmd_patterns) {
            if (cp.pattern_words.size() == 1) {
                // Single-word command
                MatchType mt = fuzzy_match(word, cp.pattern_words[0], cp.variants_list, cfg.fuzzy_max_distance);
                if (mt == MatchType::EXACT) {
                    result.command_exact_counts[cp.name]++;
                    result.command_exact_matches[cp.name].push_back(word);
                } else if (mt == MatchType::APPROXIMATE) {
                    result.command_approx_counts[cp.name]++;
                    result.command_approx_matches[cp.name].push_back(word);
                }
            } else {
                // Multi-word command (n-gram match)
                size_t n = cp.pattern_words.size();
                if (i + n <= words.size()) {
                    bool all_match = true;
                    bool all_exact = true;
                    for (size_t j = 0; j < n; j++) {
                        auto pw_variants = get_variants(cp.pattern_words[j], variants);
                        MatchType mt = fuzzy_match(words[i + j], cp.pattern_words[j], pw_variants, cfg.fuzzy_max_distance);
                        if (mt == MatchType::NONE) {
                            all_match = false;
                            break;
                        }
                        if (mt == MatchType::APPROXIMATE) {
                            all_exact = false;
                        }
                    }
                    if (all_match) {
                        std::string phrase;
                        for (size_t j = 0; j < n; j++) {
                            if (j > 0) phrase += " ";
                            phrase += words[i + j];
                        }
                        if (all_exact) {
                            result.command_exact_counts[cp.name]++;
                            result.command_exact_matches[cp.name].push_back(phrase);
                        } else {
                            result.command_approx_counts[cp.name]++;
                            result.command_approx_matches[cp.name].push_back(phrase);
                        }
                    }
                }
            }
        }

        // Check call signs
        for (const auto& cs : cfg.call_signs) {
            std::string cs_upper = to_upper(cs);
            auto cs_variants = get_variants(cs_upper, variants);
            if (fuzzy_match(word, cs_upper, cs_variants, cfg.fuzzy_max_distance) != MatchType::NONE) {
                result.call_sign_counts[cs_upper]++;
                result.call_sign_matches[cs_upper].push_back(word);
            }
        }
    }

    return result;
}
