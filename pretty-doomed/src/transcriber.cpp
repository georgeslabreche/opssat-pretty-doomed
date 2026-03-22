#include "transcriber.h"
#include <iostream>
#include <cstring>
#include <fstream>
#include <chrono>
#include "sherpa-onnx/c-api/c-api.h"
#include "pretty_log.h"

using namespace pretty;

static long long file_size_kb(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    return f.is_open() ? (long long)f.tellg() / 1024 : -1;
}

// --- Transcriber class (persistent model) ---

Transcriber::Transcriber(const PipelineConfig& cfg) {
    log_info() << "Loading STT model...\n";
    log_info() << "  Encoder: " << cfg.stt_model_encoder << " (" << file_size_kb(cfg.stt_model_encoder) << " KB)\n";
    log_info() << "  Decoder: " << cfg.stt_model_decoder << " (" << file_size_kb(cfg.stt_model_decoder) << " KB)\n";
    log_info() << "  Joiner:  " << cfg.stt_model_joiner << " (" << file_size_kb(cfg.stt_model_joiner) << " KB)\n";
    log_info() << "  Tokens:  " << cfg.stt_model_tokens << "\n";

    SherpaOnnxOfflineRecognizerConfig config;
    memset(&config, 0, sizeof(config));
    config.model_config.transducer.encoder = cfg.stt_model_encoder.c_str();
    config.model_config.transducer.decoder = cfg.stt_model_decoder.c_str();
    config.model_config.transducer.joiner  = cfg.stt_model_joiner.c_str();
    config.model_config.tokens = cfg.stt_model_tokens.c_str();
    config.model_config.num_threads = cfg.stt_num_threads;
    config.model_config.debug = 0;
    config.decoding_method = cfg.stt_decoding_method.c_str();

    auto load_start = std::chrono::steady_clock::now();
    recognizer_ = SherpaOnnxCreateOfflineRecognizer(&config);
    auto load_end = std::chrono::steady_clock::now();
    auto load_ms = std::chrono::duration_cast<std::chrono::milliseconds>(load_end - load_start).count();

    if (recognizer_) {
        log_info() << "STT model loaded (" << load_ms << " ms)\n";
    } else {
        log_error() << "Cannot create recognizer. Check model paths.\n";
    }
}

Transcriber::~Transcriber() {
    if (recognizer_) {
        log_info() << "Unloading STT model...\n";
        SherpaOnnxDestroyOfflineRecognizer(
            static_cast<const SherpaOnnxOfflineRecognizer*>(recognizer_));
        log_info() << "STT model unloaded\n";
    }
}

bool Transcriber::is_ready() const {
    return recognizer_ != nullptr;
}

std::string Transcriber::transcribe(const std::vector<float>& samples, int sample_rate) {
    if (!recognizer_) return "";

    auto* rec = static_cast<const SherpaOnnxOfflineRecognizer*>(recognizer_);
    const SherpaOnnxOfflineStream* stream = SherpaOnnxCreateOfflineStream(rec);
    if (!stream) {
        log_error() << "Cannot create stream\n";
        return "";
    }

    SherpaOnnxAcceptWaveformOffline(stream, sample_rate, samples.data(), samples.size());
    SherpaOnnxDecodeOfflineStream(rec, stream);

    const SherpaOnnxOfflineRecognizerResult* result =
        SherpaOnnxGetOfflineStreamResult(stream);

    std::string transcription = (result && result->text) ? result->text : "";

    SherpaOnnxDestroyOfflineRecognizerResult(result);
    SherpaOnnxDestroyOfflineStream(stream);

    return transcription;
}

// --- One-shot convenience function ---

std::string transcribe(const std::vector<float>& samples,
                       int sample_rate,
                       const PipelineConfig& cfg) {
    Transcriber t(cfg);
    if (!t.is_ready()) return "";
    return t.transcribe(samples, sample_rate);
}
