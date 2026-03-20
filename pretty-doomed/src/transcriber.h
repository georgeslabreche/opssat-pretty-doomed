#ifndef TRANSCRIBER_H
#define TRANSCRIBER_H

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

    // Transcribe audio samples. Returns uppercase transcription text.
    std::string transcribe(const std::vector<float>& samples, int sample_rate);

private:
    const void* recognizer_ = nullptr;
};

// Convenience: one-shot transcribe (loads and unloads model per call).
std::string transcribe(const std::vector<float>& samples,
                       int sample_rate,
                       const PipelineConfig& cfg);

#endif
