#include "output.h"
#include <vector>
#include <sstream>
#include <iomanip>

static std::string join(const std::vector<std::string>& v, const std::string& sep) {
    std::string s;
    for (size_t i = 0; i < v.size(); i++) {
        if (i > 0) s += sep;
        s += v[i];
    }
    return s;
}

static std::string to_key(const std::string& s) {
    std::string k = s;
    for (auto& c : k) { if (c == ' ') c = '_'; }
    return k;
}

// Collect all command names (union of exact and approx keys).
static std::vector<std::string> all_commands(const DetectionResult& d) {
    std::vector<std::string> cmds;
    for (const auto& [cmd, _] : d.command_exact_counts) {
        cmds.push_back(cmd);
    }
    for (const auto& [cmd, _] : d.command_approx_counts) {
        if (d.command_exact_counts.count(cmd) == 0) {
            cmds.push_back(cmd);
        }
    }
    return cmds;
}

DetectionTotals compute_totals(const DetectionResult& detection) {
    DetectionTotals t;
    for (const auto& [cmd, count] : detection.command_exact_counts) {
        t.command_exact += count;
    }
    for (const auto& [cmd, count] : detection.command_approx_counts) {
        t.command_approx += count;
    }
    t.points_exact = detection.wake_word_exact + t.command_exact;
    t.points_approx = detection.wake_word_approx + t.command_approx;
    t.points = t.points_exact + t.points_approx;
    t.command_detected = (t.command_exact + t.command_approx) >= 1;
    return t;
}

static std::string fmt_float(float v) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(4) << v;
    return oss.str();
}

std::string format_scores(const DetectionResult& detection,
                          const DetectionTotals& totals,
                          const AudioStats& audio) {
    std::string s;
    s += "wake_word_exact=" + std::to_string(detection.wake_word_exact) + "\n";
    s += "wake_word_exact_matches=" + join(detection.wake_word_exact_matches, ",") + "\n";
    s += "wake_word_approx=" + std::to_string(detection.wake_word_approx) + "\n";
    s += "wake_word_approx_matches=" + join(detection.wake_word_approx_matches, ",") + "\n";

    for (const auto& cmd : all_commands(detection)) {
        int ec = 0, ac = 0;
        auto eit = detection.command_exact_counts.find(cmd);
        if (eit != detection.command_exact_counts.end()) ec = eit->second;
        auto ait = detection.command_approx_counts.find(cmd);
        if (ait != detection.command_approx_counts.end()) ac = ait->second;

        std::string key = "command_" + to_key(cmd);
        s += key + "_exact=" + std::to_string(ec) + "\n";
        auto emit = detection.command_exact_matches.find(cmd);
        s += key + "_exact_matches=" +
             (emit != detection.command_exact_matches.end() ? join(emit->second, ",") : "") + "\n";
        s += key + "_approx=" + std::to_string(ac) + "\n";
        auto amit = detection.command_approx_matches.find(cmd);
        s += key + "_approx_matches=" +
             (amit != detection.command_approx_matches.end() ? join(amit->second, ",") : "") + "\n";
    }

    for (const auto& [sign, count] : detection.call_sign_counts) {
        s += "call_sign_" + sign + "=" + std::to_string(count) + "\n";
        auto it = detection.call_sign_matches.find(sign);
        if (it != detection.call_sign_matches.end()) {
            s += "call_sign_" + sign + "_matches=" + join(it->second, ",") + "\n";
        }
    }

    s += "total_points=" + std::to_string(totals.points) + "\n";
    s += "total_points_exact=" + std::to_string(totals.points_exact) + "\n";
    s += "total_points_approx=" + std::to_string(totals.points_approx) + "\n";
    s += "audio_rms_raw=" + fmt_float(audio.rms_raw) + "\n";
    s += "audio_peak_raw=" + fmt_float(audio.peak_raw) + "\n";
    s += "audio_rms_filtered=" + fmt_float(audio.rms_filtered) + "\n";
    s += "audio_peak_filtered=" + fmt_float(audio.peak_filtered) + "\n";
    return s;
}

std::string format_summary(const DetectionResult& detection,
                           const DetectionTotals& totals,
                           const PipelineConfig& cfg,
                           const std::string& input_file,
                           const std::string& transcript,
                           const std::string& ascii_art) {
    std::string s;
    s += "=== PRETTY DOOMed Summary ===\n\n";
    s += "Input: " + input_file + "\n";
    s += "Transcription: " + transcript + "\n\n";
    s += "Detection:\n";
    s += "  Wake word (" + cfg.wake_word + "):\n";
    s += "    Exact: " + std::to_string(detection.wake_word_exact) +
         " [" + join(detection.wake_word_exact_matches, ", ") + "]\n";
    s += "    Approximate: " + std::to_string(detection.wake_word_approx) +
         " [" + join(detection.wake_word_approx_matches, ", ") + "]\n";

    for (const auto& cmd : all_commands(detection)) {
        int ec = 0, ac = 0;
        auto eit = detection.command_exact_counts.find(cmd);
        if (eit != detection.command_exact_counts.end()) ec = eit->second;
        auto ait = detection.command_approx_counts.find(cmd);
        if (ait != detection.command_approx_counts.end()) ac = ait->second;

        s += "  Command [" + cmd + "]:\n";
        auto emit = detection.command_exact_matches.find(cmd);
        s += "    Exact: " + std::to_string(ec) + " [" +
             (emit != detection.command_exact_matches.end() ? join(emit->second, ", ") : "") + "]\n";
        auto amit = detection.command_approx_matches.find(cmd);
        s += "    Approximate: " + std::to_string(ac) + " [" +
             (amit != detection.command_approx_matches.end() ? join(amit->second, ", ") : "") + "]\n";
    }

    for (const auto& [sign, count] : detection.call_sign_counts) {
        s += "  Call sign [" + sign + "]: " + std::to_string(count) + "\n";
        auto it = detection.call_sign_matches.find(sign);
        if (it != detection.call_sign_matches.end()) {
            s += "    Matches: " + join(it->second, ", ") + "\n";
        }
    }

    s += "\nTotal points: " + std::to_string(totals.points) +
         " (exact=" + std::to_string(totals.points_exact) +
         ", approx=" + std::to_string(totals.points_approx) + ")\n\n";

    if (totals.command_detected) {
        s += "Result: COMMAND DETECTED - launching DOOM\n";
        if (!ascii_art.empty()) {
            s += "\n" + ascii_art;
        }
    } else {
        s += "Result: No command detected\n";
    }

    return s;
}
