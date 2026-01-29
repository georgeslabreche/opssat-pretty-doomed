#ifndef MATCHER_H
#define MATCHER_H

#include <string>
#include <vector>
#include <map>
#include "config.h"

// Convert string to uppercase.
std::string to_upper(const std::string& s);

// Split text into words on whitespace.
std::vector<std::string> split_words(const std::string& text);

// Levenshtein edit distance between two strings.
int levenshtein(const std::string& a, const std::string& b);

enum class MatchType { NONE, EXACT, APPROXIMATE };

// Check if candidate matches target or any of its variants within max_distance.
MatchType fuzzy_match(const std::string& candidate,
                      const std::string& target,
                      const std::vector<std::string>& variants,
                      int max_distance);

struct DetectionResult {
    int wake_word_exact = 0;
    int wake_word_approx = 0;
    std::vector<std::string> wake_word_exact_matches;
    std::vector<std::string> wake_word_approx_matches;
    std::map<std::string, int> command_exact_counts;
    std::map<std::string, int> command_approx_counts;
    std::map<std::string, std::vector<std::string>> command_exact_matches;
    std::map<std::string, std::vector<std::string>> command_approx_matches;
    std::map<std::string, int> call_sign_counts;
    std::map<std::string, std::vector<std::string>> call_sign_matches;
};

// Scan transcript for wake word, call signs, and command.
DetectionResult detect(const std::string& transcript,
                       const PipelineConfig& cfg,
                       const VariantsMap& variants);

#endif
