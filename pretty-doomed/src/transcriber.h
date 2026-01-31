#ifndef TRANSCRIBER_H
#define TRANSCRIBER_H

#include <string>
#include <vector>
#include "config.h"

// Transcribe audio samples using sherpa-onnx offline recognition.
// Model paths are read from cfg (model_encoder, model_decoder, model_joiner, model_tokens).
// Returns uppercase transcription text.
std::string transcribe(const std::vector<float>& samples,
                       int sample_rate,
                       const PipelineConfig& cfg);

#endif
