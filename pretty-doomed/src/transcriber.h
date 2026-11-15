#ifndef TRANSCRIBER_H
#define TRANSCRIBER_H

#include <cstddef>
#include <string>
#include <vector>
#include "config.h"

// Persistent transcriber: loads the model once, reuses across calls.
class Transcriber {
public:
    // Loads the STT model. Check is_ready() after construction.
    explicit Transcriber(const PipelineConfig& cfg);
    ~Transcriber();

    // Non-copyable (owns model resources)
    Transcriber(const Transcriber&) = delete;
    Transcriber& operator=(const Transcriber&) = delete;

    bool is_ready() const;

    // Transcribe audio samples. With stt_window_s > 0, the audio is cut into
    // windows and the window texts are joined with spaces.
    std::string transcribe(const std::vector<float>& samples, int sample_rate);

    // Model load time in milliseconds.
    long long load_ms() const { return load_ms_; }

private:
    std::string transcribe_window(const float* samples, std::size_t n, int sample_rate);

    const void* recognizer_ = nullptr;
    double window_s_ = 0.0;
    long long load_ms_ = -1;
};

// Convenience: one-shot transcribe (loads and unloads model per call).
std::string transcribe(const std::vector<float>& samples,
                       int sample_rate,
                       const PipelineConfig& cfg);

#endif
