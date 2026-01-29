#include "config.h"
#include <fstream>
#include <sstream>
#include <algorithm>

static std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static std::vector<std::string> split_csv(const std::string& s) {
    std::vector<std::string> result;
    std::istringstream stream(s);
    std::string token;
    while (std::getline(stream, token, ',')) {
        std::string trimmed = trim(token);
        if (!trimmed.empty()) {
            result.push_back(trimmed);
        }
    }
    return result;
}

bool load_config(std::istream& stream, PipelineConfig& cfg) {
    std::string line;
    while (std::getline(stream, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = trim(line.substr(0, eq));
        std::string value = trim(line.substr(eq + 1));

        if (key == "lowpass_cutoff") cfg.lowpass_cutoff = std::stof(value);
        else if (key == "lowpass_transition") cfg.lowpass_transition = std::stof(value);
        else if (key == "bandpass_low") cfg.bandpass_low = std::stof(value);
        else if (key == "bandpass_high") cfg.bandpass_high = std::stof(value);
        else if (key == "bandpass_transition") cfg.bandpass_transition = std::stof(value);
        else if (key == "model_encoder") cfg.model_encoder = value;
        else if (key == "model_decoder") cfg.model_decoder = value;
        else if (key == "model_joiner") cfg.model_joiner = value;
        else if (key == "model_tokens") cfg.model_tokens = value;
        else if (key == "decoding_method") cfg.decoding_method = value;
        else if (key == "num_threads") cfg.num_threads = std::stoi(value);
        else if (key == "wake_word") cfg.wake_word = value;
        else if (key == "call_signs") cfg.call_signs = split_csv(value);
        else if (key == "command") cfg.commands = split_csv(value);
        else if (key == "fuzzy_max_distance") cfg.fuzzy_max_distance = std::stoi(value);
    }
    return true;
}

bool load_config(const std::string& path, PipelineConfig& cfg) {
    std::ifstream file(path);
    if (!file.is_open()) return false;
    return load_config(file, cfg);
}

bool load_variants(std::istream& stream, VariantsMap& variants) {
    std::string line;
    while (std::getline(stream, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string target = trim(line.substr(0, eq));
        std::string values = trim(line.substr(eq + 1));

        variants[target] = split_csv(values);
    }
    return true;
}

bool load_variants(const std::string& path, VariantsMap& variants) {
    std::ifstream file(path);
    if (!file.is_open()) return false;
    return load_variants(file, variants);
}
