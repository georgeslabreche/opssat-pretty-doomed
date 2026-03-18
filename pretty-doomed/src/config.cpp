#include "config.h"
#include <fstream>
#include <sstream>
#include <algorithm>

#include "pretty_log.h"
#include "pretty_config.h"

using namespace pretty;

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

// Map a key-value pair to PipelineConfig fields.
// Shared by both the stream and file overloads.
static void apply_config_entry(const std::string& key, const std::string& value,
                                PipelineConfig& cfg) {
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
    else if (key == "doom_keepgifframes") cfg.doom_keepgifframes = (value == "true" || value == "1");
    else {
        const std::string frames_prefix = "doom_frames_";
        const std::string maxframes_prefix = "doom_maxframes_";
        if (key.compare(0, maxframes_prefix.size(), maxframes_prefix) == 0) {
            std::string demo = key.substr(maxframes_prefix.size());
            cfg.doom_maxframes[demo] = std::stoi(value);
        } else if (key.compare(0, frames_prefix.size(), frames_prefix) == 0) {
            std::string demo = key.substr(frames_prefix.size());
            cfg.doom_frames[demo] = value;
        }
    }
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
        apply_config_entry(key, value, cfg);
    }
    return true;
}

bool load_config(const std::string& path, PipelineConfig& cfg) {
    auto result = load_config_map(path);
    if (!result.ok) return false;

    for (const auto& [key, value] : result.values) {
        apply_config_entry(key, value, cfg);
    }
    return true;
}

bool load_variants(std::istream& stream, VariantsMap& variants) {
    std::string line;
    while (std::getline(stream, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string target = trim(line.substr(0, eq));
        std::string values_str = trim(line.substr(eq + 1));

        variants[target] = split_csv(values_str);
    }
    return true;
}

bool load_variants(const std::string& path, VariantsMap& variants) {
    std::ifstream file(path);
    if (!file.is_open()) return false;
    return load_variants(file, variants);
}
