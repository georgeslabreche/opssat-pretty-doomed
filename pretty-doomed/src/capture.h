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

// Replay a capture from an sc16 file instead of the AD9361 (#116): the input
// is treated as a capture.sc16 (post-channel-LPF complex baseband at the
// effective rate, interleaved int16) and runs the identical post-capture
// processing as a live capture: demod to capture.wav, the config-gated
// narrowing, RMS normalization, and I/Q artifacts. For EM validation without
// SDR hardware or the IIO emulator.
bool run_capture_from_file(const PipelineConfig& cfg,
                           const std::string& sc16_input,
                           const std::string& output_dir,
                           CaptureResult& result);

#endif
