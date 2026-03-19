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
    // SDR capture
    else if (key == "sdr_frequency") cfg.sdr_frequency = std::stoll(value);
    else if (key == "sdr_rate") cfg.sdr_rate = std::stol(value);
    else if (key == "sdr_decimation") cfg.sdr_decimation = std::stoi(value);
    else if (key == "sdr_rf_bandwidth") cfg.sdr_rf_bandwidth = std::stol(value);
    else if (key == "sdr_gain") cfg.sdr_gain = std::stod(value);
    else if (key == "sdr_fm_deviation") cfg.sdr_fm_deviation = std::stod(value);
    else if (key == "sdr_uri") cfg.sdr_uri = value;
    else if (key == "sdr_duration") cfg.sdr_duration = std::stoi(value);
    else if (key == "sdr_max_iq_mb") cfg.sdr_max_iq_mb = std::stoi(value);
    else if (key == "sdr_audio_rate") cfg.sdr_audio_rate = std::stoi(value);
    else if (key == "sdr_lpf_cutoff") cfg.sdr_lpf_cutoff = std::stod(value);
    else if (key == "sdr_lpf_transition") cfg.sdr_lpf_transition = std::stod(value);
    else if (key == "sdr_bandpass_low") cfg.sdr_bandpass_low = std::stod(value);
    else if (key == "sdr_bandpass_high") cfg.sdr_bandpass_high = std::stod(value);
    else if (key == "sdr_min_readback") cfg.sdr_min_readback = (value == "true" || value == "1");
    else if (key == "sdr_single_core") cfg.sdr_single_core = (value == "true" || value == "1");
    else if (key == "sdr_rate_tolerance") cfg.sdr_rate_tolerance = std::stol(value);
    else if (key == "sdr_timeout_multiplier") cfg.sdr_timeout_multiplier = std::stoi(value);
    else if (key == "sdr_enable_spectrogram") cfg.sdr_enable_spectrogram = (value == "true" || value == "1");
    else if (key == "sdr_enable_constellation") cfg.sdr_enable_constellation = (value == "true" || value == "1");
    else if (key == "sdr_captures") cfg.sdr_captures = std::stoi(value);
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
