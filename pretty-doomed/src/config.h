#ifndef CONFIG_H
#define CONFIG_H

#include <string>
#include <vector>
#include <unordered_map>
#include <iostream>

// Variant lookup: TARGET -> [VARIANT1, VARIANT2, ...]
using VariantsMap = std::unordered_map<std::string, std::vector<std::string>>;

struct PipelineConfig {
    // Signal processing
    float lowpass_cutoff = 3400.0f;
    float lowpass_transition = 500.0f;
    float bandpass_low = 300.0f;
    float bandpass_high = 3400.0f;
    float bandpass_transition = 100.0f;

    // Speech-to-text model paths
    std::string model_encoder;
    std::string model_decoder;
    std::string model_joiner;
    std::string model_tokens;
    std::string decoding_method = "modified_beam_search";
    int num_threads = 1;

    // Detection
    std::string wake_word = "PRETTY";
    std::vector<std::string> call_signs;
    std::vector<std::string> commands = {"DOOM"};
    int fuzzy_max_distance = 2;
};

// Parse KEY=VALUE config file. Lines starting with # are comments.
bool load_config(const std::string& path, PipelineConfig& cfg);
bool load_config(std::istream& stream, PipelineConfig& cfg);

// Parse TARGET=VARIANT1,VARIANT2,... file. Lines starting with # are comments.
bool load_variants(const std::string& path, VariantsMap& variants);
bool load_variants(std::istream& stream, VariantsMap& variants);

#endif
