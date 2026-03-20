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

    // DOOM frame capture: demo name -> frames spec (e.g. "5000,5001-5020")
    // Special values: "-1" = random frame, comma list = cycling
    std::unordered_map<std::string, std::string> doom_frames;
    // Max frame count per demo (for random frame selection)
    std::unordered_map<std::string, int> doom_maxframes;
    bool doom_keepgifframes = false;
    std::vector<std::string> doom_demo_order;  // optional: cycle order (default: alphabetical)

    // SDR capture (used when --sdr-capture mode is active)
    long long sdr_frequency = 1296000000;
    long sdr_rate = 2400000;
    int sdr_decimation = 12;
    long sdr_rf_bandwidth = 200000;
    double sdr_gain = 50.0;
    double sdr_fm_deviation = 5000.0;
    std::string sdr_uri = "local:";
    int sdr_duration = 20;
    int sdr_max_iq_mb = 20;
    int sdr_audio_rate = 16000;
    double sdr_lpf_cutoff = 85000.0;
    double sdr_lpf_transition = 15000.0;
    double sdr_bandpass_low = 300.0;
    double sdr_bandpass_high = 3400.0;
    bool sdr_min_readback = false;
    bool sdr_single_core = false;
    long sdr_rate_tolerance = 10;
    int sdr_timeout_multiplier = 5;
    bool sdr_enable_spectrogram = true;
    bool sdr_enable_constellation = true;
    int sdr_captures = 3;              // number of sequential SDR captures
    std::string process_mode = "sequential";  // "sequential" or "background"
};

// Parse KEY=VALUE config file. Lines starting with # are comments.
bool load_config(const std::string& path, PipelineConfig& cfg);
bool load_config(std::istream& stream, PipelineConfig& cfg);

// Parse TARGET=VARIANT1,VARIANT2,... file. Lines starting with # are comments.
bool load_variants(const std::string& path, VariantsMap& variants);
bool load_variants(std::istream& stream, VariantsMap& variants);

#endif
