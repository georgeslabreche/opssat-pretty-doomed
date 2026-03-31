#ifndef CONFIG_H
#define CONFIG_H

#include <string>
#include <vector>
#include <unordered_map>
#include <iostream>

// Variant lookup: TARGET -> [VARIANT1, VARIANT2, ...]
using VariantsMap = std::unordered_map<std::string, std::vector<std::string>>;

struct PipelineConfig {
    // Operation: what the voice command triggers (feature flag)
    std::string operation = "doom";     // "doom" (only implementation for now)

    // DSP: signal processing filters
    float dsp_lowpass_cutoff = 3400.0f;
    float dsp_lowpass_transition = 500.0f;
    float dsp_bandpass_low = 300.0f;
    float dsp_bandpass_high = 3400.0f;
    float dsp_bandpass_transition = 100.0f;

    // STT: speech-to-text model
    std::string stt_model_encoder;
    std::string stt_model_decoder;
    std::string stt_model_joiner;
    std::string stt_model_tokens;
    std::string stt_decoding_method = "modified_beam_search";
    int stt_num_threads = 1;
    bool stt_concurrent_load = false;

    // Detection: command recognition
    std::string detect_wake_word = "PRETTY";
    std::vector<std::string> detect_call_signs;
    std::vector<std::string> detect_commands = {"DOOM"};
    int detect_fuzzy_max_distance = 2;

    // DOOM: frame capture and execution
    std::unordered_map<std::string, std::string> doom_frames;
    std::unordered_map<std::string, int> doom_maxframes;
    bool doom_keepgifframes = false;
    std::vector<std::string> doom_demo_order;
    bool doom_force_trigger = false;
    bool doom_enable_postcard = true;
    int doom_postcard_scale = 1;
    std::string doom_assets_dir = "assets";

    // SDR: capture settings
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
    int sdr_captures = 3;
    std::string process_mode = "sequential";

    // SDR: hardware FIR decimation (AD9361 programmable FIR via libad9361)
    bool sdr_hw_fir_enable = false;
    long sdr_hw_fir_rate = 0;         // Post-FIR baseband rate in Hz (e.g. 600000)
    long sdr_hw_fir_fpass = 0;        // Passband edge frequency in Hz
    long sdr_hw_fir_fstop = 0;        // Stopband edge frequency in Hz
    long sdr_hw_fir_wnom_tx = 0;      // TX analog filter bandwidth in Hz
    long sdr_hw_fir_wnom_rx = 0;      // RX analog filter bandwidth in Hz
};

// Parse KEY=VALUE config file. Lines starting with # are comments.
bool load_config(const std::string& path, PipelineConfig& cfg);
bool load_config(std::istream& stream, PipelineConfig& cfg);

// Parse TARGET=VARIANT1,VARIANT2,... file. Lines starting with # are comments.
bool load_variants(const std::string& path, VariantsMap& variants);
bool load_variants(std::istream& stream, VariantsMap& variants);

#endif
