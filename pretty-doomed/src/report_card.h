#ifndef REPORT_CARD_H
#define REPORT_CARD_H

// Report card of one distress clip (#142): a PNG made on board with the
// verdict, the run numbers, the audio spectrogram with the STT windows, the
// windows with a distress phrase, the matched phrases, and a transcript
// excerpt. The distress counterpart of the DOOM postcard.
//
// No external dependencies (own FFT, bitmap font, stb_image_write): unit-tested
// in tests/test_report_card.cpp.

#include <string>
#include <utility>
#include <vector>
#include "config.h"
#include "matcher.h"

constexpr int REPORT_CARD_WIDTH = 1200;
constexpr int REPORT_CARD_HEIGHT = 780;

// One transcript word on the card. marked: part of a matched phrase.
// approximate: part of an approximate match only (one letter different).
struct CardWord {
    std::string text;
    bool marked = false;
    bool approximate = false;
    bool operator==(const CardWord& o) const {
        return text == o.text && marked == o.marked && approximate == o.approximate;
    }
};

// A matched phrase and its count. heard: the words that matched, for an
// approximate match (each once, in order).
struct PhraseCount {
    std::string phrase;
    int count = 0;
    std::vector<std::string> heard;
    bool operator==(const PhraseCount& o) const {
        return phrase == o.phrase && count == o.count && heard == o.heard;
    }
};

enum class WindowMatch { None, Approximate, Exact };

struct MatchCounts {
    std::vector<PhraseCount> exact;
    std::vector<PhraseCount> approximate;
};

struct ReportCardData {
    std::string clip;            // file name without the extension
    std::string config_name;     // config file name without "config.distress." and ".cfg"
    std::string model_type;
    std::string decoding;
    double window_s = 0.0;
    double audio_s = 0.0;
    double stt_s = 0.0;
    long long peak_rss_kb = -1;  // VmHWM when the card is made; -1 if not known
    bool detected = false;
    std::vector<WindowMatch> window_hits;  // one per STT window
    MatchCounts matches;
    std::vector<CardWord> excerpt;
    std::string generated_utc;
};

// For each window text: Exact if detect() finds an exact command phrase in
// it, Approximate if it finds only approximate matches, else None. The wake
// word alone does not mark a window.
std::vector<WindowMatch> window_matches(const std::vector<std::string>& window_texts,
                                        const PipelineConfig& cfg,
                                        const VariantsMap& variants);

// Count of each matched phrase, exact and approximate apart, highest count
// first, then by name. The wake word is included under wake_word when it matched.
MatchCounts match_counts(const DetectionResult& detection, const std::string& wake_word = "");

// The transcript words around the matched phrases, with context_words words
// on each side, and "..." for the words that are left out. Words are compared
// without punctuation and case. Without a match: the start of the transcript.
// The excerpt stops at about max_chars characters.
std::vector<CardWord> match_excerpt(const std::string& transcript,
                                    const DetectionResult& detection,
                                    int context_words,
                                    std::size_t max_chars);

// Greedy line wrap: each line has at most width characters (words and spaces),
// except a single word that is longer than width.
std::vector<std::vector<CardWord>> wrap_words(const std::vector<CardWord>& words, std::size_t width);

// Magnitude spectrogram in dB: columns spectra of fft_size samples (Hann
// window), evenly spaced over the audio, each with the bins from 0 Hz up to
// max_hz. fft_size must be a power of two.
std::vector<std::vector<float>> audio_spectrogram(const std::vector<float>& samples,
                                                  int sample_rate,
                                                  int columns,
                                                  int fft_size,
                                                  double max_hz);

// Draw the card and write it as a PNG. Returns false if the file cannot be written.
bool render_report_card(const ReportCardData& data,
                        const std::vector<float>& samples,
                        int sample_rate,
                        const std::string& png_path);

#endif
