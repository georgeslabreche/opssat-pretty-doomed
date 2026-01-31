#ifndef OUTPUT_H
#define OUTPUT_H

#include <string>
#include "config.h"
#include "matcher.h"

struct AudioStats {
    float rms_raw = 0.0f;
    float peak_raw = 0.0f;
    float rms_filtered = 0.0f;
    float peak_filtered = 0.0f;
};

struct DetectionTotals {
    int command_exact = 0;
    int command_approx = 0;
    int points_exact = 0;
    int points_approx = 0;
    int points = 0;
    bool command_detected = false;
};

// Compute aggregate totals from detection result.
DetectionTotals compute_totals(const DetectionResult& detection);

// Format machine-readable scores (key=value lines for scores.txt).
std::string format_scores(const DetectionResult& detection,
                          const DetectionTotals& totals,
                          const AudioStats& audio = {});

// Format human-readable summary (for summary.txt).
// ascii_art is appended after "COMMAND DETECTED" when non-empty.
std::string format_summary(const DetectionResult& detection,
                           const DetectionTotals& totals,
                           const PipelineConfig& cfg,
                           const std::string& input_file,
                           const std::string& transcript,
                           const std::string& ascii_art = "");

#endif
