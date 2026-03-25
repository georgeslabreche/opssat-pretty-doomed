#ifndef CAPTURE_H
#define CAPTURE_H

#include <string>
#include <future>
#include "config.h"

struct CaptureResult {
    std::string wav_path;
    std::string sc16_path;
    double duration_sec = 0.0;
    bool success = false;
    std::shared_future<void> artifact_future;  // I/Q diagnostics + BMP artifacts
};

// Capture RF audio from AD9361, demodulate FM, write WAV and sc16.
// Output files are written to output_dir.
// Returns true on success, populates result with output paths.
bool run_capture(const PipelineConfig& cfg,
                 const std::string& output_dir,
                 CaptureResult& result);

#endif
