#include "report_card.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <map>
#include <set>
#include <sstream>

#include "pretty_font.h"
#include "stb_image_write.h"

using pretty::draw_text;
using pretty::GLYPH_W;
using pretty::text_width;

namespace {

constexpr double PI = 3.14159265358979323846;

struct Rgb {
    uint8_t r, g, b;
};

// Colors: dark card surface, light ink, and the status colors of the report.
constexpr Rgb SURFACE{26, 26, 25};
constexpr Rgb PANEL{38, 38, 36};
constexpr Rgb INK{240, 239, 236};
constexpr Rgb INK_2{195, 194, 183};
constexpr Rgb GRID{90, 89, 84};
constexpr Rgb CRITICAL{208, 59, 59};
constexpr Rgb MARK{250, 178, 25};
constexpr Rgb NEAR{236, 131, 90};

// Spectrogram ramp: dark to bright.
constexpr Rgb RAMP[] = {{26, 26, 25}, {16, 66, 129}, {42, 120, 214}, {27, 175, 122}, {237, 161, 0}, {252, 252, 251}};
constexpr int RAMP_N = sizeof(RAMP) / sizeof(RAMP[0]);

std::string upper_word(const std::string& w) {
    std::string out;
    for (char c : w) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '\'') {
            out += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
    }
    return out;
}

std::vector<std::string> card_words(const std::string& text) {
    std::vector<std::string> words;
    std::istringstream in(text);
    std::string w;
    while (in >> w) words.push_back(w);
    return words;
}

void fft(std::vector<std::complex<float>>& a) {
    const std::size_t n = a.size();
    for (std::size_t i = 1, j = 0; i < n; i++) {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1) {
        const float ang = static_cast<float>(-2 * PI / len);
        const std::complex<float> wlen(std::cos(ang), std::sin(ang));
        for (std::size_t i = 0; i < n; i += len) {
            std::complex<float> w(1);
            for (std::size_t j = 0; j < len / 2; j++) {
                const auto u = a[i + j];
                const auto v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

class Canvas {
public:
    Canvas(int w, int h) : w_(w), h_(h), px_(static_cast<std::size_t>(w) * h * 3) {}
    void fill(int x, int y, int w, int h, Rgb c) {
        for (int yy = std::max(0, y); yy < std::min(h_, y + h); yy++) {
            for (int xx = std::max(0, x); xx < std::min(w_, x + w); xx++) set(xx, yy, c);
        }
    }
    void set(int x, int y, Rgb c) {
        if (x < 0 || y < 0 || x >= w_ || y >= h_) return;
        auto i = (static_cast<std::size_t>(y) * w_ + x) * 3;
        px_[i] = c.r;
        px_[i + 1] = c.g;
        px_[i + 2] = c.b;
    }
    // Returns the x after the text.
    int text(int x, int y, const std::string& s, Rgb c, int scale) {
        draw_text(px_, w_, h_, x, y, s, c.r, c.g, c.b, scale);
        return x + text_width(s, scale) + (GLYPH_W + 1) * scale;
    }
    bool write_png(const std::string& path) const {
        return stbi_write_png(path.c_str(), w_, h_, 3, px_.data(), w_ * 3) != 0;
    }

private:
    int w_, h_;
    std::vector<uint8_t> px_;
};

// 16 steps, so that the PNG compresses well.
Rgb ramp(float t) {
    t = std::floor(std::clamp(t, 0.0f, 1.0f) * 15.0f) / 15.0f * (RAMP_N - 1);
    int i = std::min(static_cast<int>(t), RAMP_N - 2);
    float f = t - i;
    auto mix = [f](uint8_t a, uint8_t b) { return static_cast<uint8_t>(a + (b - a) * f); };
    return {mix(RAMP[i].r, RAMP[i + 1].r), mix(RAMP[i].g, RAMP[i + 1].g), mix(RAMP[i].b, RAMP[i + 1].b)};
}

std::string fixed(double v, int decimals) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, v);
    return buf;
}

}  // namespace

std::vector<WindowMatch> window_matches(const std::vector<std::string>& window_texts,
                                        const PipelineConfig& cfg,
                                        const VariantsMap& variants) {
    std::vector<WindowMatch> out;
    for (const auto& text : window_texts) {
        DetectionResult d = detect(text, cfg, variants);
        bool exact = false, approx = false;
        for (const auto& [name, n] : d.command_exact_counts) exact = exact || n > 0;
        for (const auto& [name, n] : d.command_approx_counts) approx = approx || n > 0;
        out.push_back(exact ? WindowMatch::Exact : approx ? WindowMatch::Approximate : WindowMatch::None);
    }
    return out;
}

namespace {

std::vector<PhraseCount> sorted_counts(const std::map<std::string, int>& counts,
                                       const std::map<std::string, std::vector<std::string>>& heard) {
    std::vector<PhraseCount> out;
    for (const auto& [name, n] : counts) {
        if (n <= 0) continue;
        PhraseCount pc{name, n, {}};
        auto it = heard.find(name);
        if (it != heard.end()) {
            for (const auto& w : it->second) {
                if (std::find(pc.heard.begin(), pc.heard.end(), w) == pc.heard.end()) pc.heard.push_back(w);
            }
        }
        out.push_back(pc);
    }
    std::stable_sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.count > b.count; });
    return out;
}

}  // namespace

MatchCounts match_counts(const DetectionResult& detection, const std::string& wake_word) {
    std::map<std::string, int> exact(detection.command_exact_counts.begin(), detection.command_exact_counts.end());
    std::map<std::string, int> approx(detection.command_approx_counts.begin(), detection.command_approx_counts.end());
    std::map<std::string, std::vector<std::string>> heard(detection.command_approx_matches.begin(),
                                                          detection.command_approx_matches.end());
    // A wake word that is also a command phrase is counted once, as the command.
    const bool wake_is_command = detection.command_exact_counts.count(wake_word) ||
                                 detection.command_approx_counts.count(wake_word);
    if (!wake_word.empty() && !wake_is_command) {
        exact[wake_word] += detection.wake_word_exact;
        approx[wake_word] += detection.wake_word_approx;
        auto& h = heard[wake_word];
        h.insert(h.end(), detection.wake_word_approx_matches.begin(), detection.wake_word_approx_matches.end());
    }
    return {sorted_counts(exact, {}), sorted_counts(approx, heard)};
}

std::vector<CardWord> match_excerpt(const std::string& transcript,
                                    const DetectionResult& detection,
                                    int context_words,
                                    std::size_t max_chars) {
    const auto words = card_words(transcript);
    std::vector<std::string> keys;
    for (const auto& w : words) keys.push_back(upper_word(w));

    // Matched phrases as word sequences, exact and approximate apart.
    std::set<std::vector<std::string>> exact, approx;
    auto add = [&](std::set<std::vector<std::string>>& to, const std::vector<std::string>& matches) {
        for (const auto& m : matches) {
            std::vector<std::string> seq;
            for (const auto& w : card_words(m)) seq.push_back(upper_word(w));
            if (!seq.empty()) to.insert(seq);
        }
    };
    add(exact, detection.wake_word_exact_matches);
    add(approx, detection.wake_word_approx_matches);
    for (const auto& [name, m] : detection.command_exact_matches) add(exact, m);
    for (const auto& [name, m] : detection.command_approx_matches) add(approx, m);

    // An exact match wins over an approximate match of the same word.
    std::vector<bool> marked(words.size(), false), approximate(words.size(), false);
    auto mark = [&](const std::set<std::vector<std::string>>& phrases, bool is_approx) {
        for (const auto& seq : phrases) {
            for (std::size_t i = 0; i + seq.size() <= words.size(); i++) {
                if (!std::equal(seq.begin(), seq.end(), keys.begin() + static_cast<long>(i))) continue;
                for (std::size_t j = 0; j < seq.size(); j++) {
                    if (is_approx && marked[i + j]) continue;
                    marked[i + j] = true;
                    approximate[i + j] = is_approx;
                }
            }
        }
    };
    mark(exact, false);
    mark(approx, true);

    // The whole transcript when it fits.
    std::size_t total = 0;
    for (const auto& w : words) total += w.size() + (total ? 1 : 0);
    if (total <= max_chars) {
        std::vector<CardWord> all;
        for (std::size_t i = 0; i < words.size(); i++) all.push_back(CardWord{words[i], marked[i], approximate[i]});
        return all;
    }

    std::vector<CardWord> out;
    std::size_t chars = 0;
    auto push = [&](const CardWord& w) {
        if (chars + w.text.size() + (out.empty() ? 0 : 1) > max_chars) return false;
        chars += w.text.size() + (out.empty() ? 0 : 1);
        out.push_back(w);
        return true;
    };
    auto ellipsis = [&]() {
        if (out.empty() || out.back().text != "...") out.push_back(CardWord{"...", false, false});
    };

    if (std::none_of(marked.begin(), marked.end(), [](bool b) { return b; })) {
        for (const auto& w : words) {
            if (!push(CardWord{w, false, false})) {
                ellipsis();
                break;
            }
        }
        return out;
    }

    // Ranges [first, last] around the marked words, merged when they touch.
    std::vector<std::pair<long, long>> ranges;
    const long n = static_cast<long>(words.size());
    for (long i = 0; i < n; i++) {
        if (!marked[i]) continue;
        long a = std::max(0L, i - context_words), b = std::min(n - 1, i + context_words);
        if (!ranges.empty() && a <= ranges.back().second + 1) {
            ranges.back().second = std::max(ranges.back().second, b);
        } else {
            ranges.emplace_back(a, b);
        }
    }
    for (const auto& [a, b] : ranges) {
        if (a > 0) ellipsis();
        for (long i = a; i <= b; i++) {
            if (!push(CardWord{words[i], marked[i], approximate[i]})) {
                ellipsis();
                return out;
            }
        }
    }
    if (ranges.back().second < n - 1) ellipsis();
    return out;
}

bool transcript_is_whole(const std::vector<CardWord>& words) {
    return std::none_of(words.begin(), words.end(), [](const CardWord& w) { return w.text == "..."; });
}

std::vector<std::vector<CardWord>> wrap_words(const std::vector<CardWord>& words, std::size_t width) {
    std::vector<std::vector<CardWord>> lines;
    std::size_t len = 0;
    for (const auto& w : words) {
        if (lines.empty() || (len > 0 && len + 1 + w.text.size() > width)) {
            lines.emplace_back();
            len = 0;
        }
        len += (len > 0 ? 1 : 0) + w.text.size();
        lines.back().push_back(w);
    }
    return lines;
}

std::vector<std::vector<float>> audio_spectrogram(const std::vector<float>& samples,
                                                  int sample_rate,
                                                  int columns,
                                                  int fft_size,
                                                  double max_hz) {
    const int bins = std::min(fft_size / 2, static_cast<int>(max_hz * fft_size / sample_rate));
    std::vector<std::vector<float>> spec(columns, std::vector<float>(bins, -120.0f));
    std::vector<float> hann(fft_size);
    for (int i = 0; i < fft_size; i++) {
        hann[i] = static_cast<float>(0.5 - 0.5 * std::cos(2 * PI * i / (fft_size - 1)));
    }
    const std::size_t span = samples.size() > static_cast<std::size_t>(fft_size)
                                 ? samples.size() - fft_size : 0;
    std::vector<std::complex<float>> buf(fft_size);
    for (int c = 0; c < columns; c++) {
        const std::size_t start = columns > 1 ? span * c / (columns - 1) : 0;
        for (int i = 0; i < fft_size; i++) {
            std::size_t k = start + i;
            buf[i] = k < samples.size() ? samples[k] * hann[i] : 0.0f;
        }
        fft(buf);
        for (int b = 0; b < bins; b++) {
            spec[c][b] = 20.0f * std::log10(std::abs(buf[b]) + 1e-9f);
        }
    }
    return spec;
}

bool render_report_card(const ReportCardData& data,
                        const std::vector<float>& samples,
                        int sample_rate,
                        const std::string& png_path) {
    const int W = REPORT_CARD_WIDTH, H = REPORT_CARD_HEIGHT, M = 20;
    Canvas cv(W, H);
    cv.fill(0, 0, W, H, SURFACE);

    // Verdict and clip.
    cv.fill(0, 0, W, 6, data.detected ? CRITICAL : GRID);
    cv.text(M, 22, data.detected ? "DISTRESS DETECTED" : "NO DISTRESS PHRASE", data.detected ? CRITICAL : INK_2, 4);
    std::string clip = data.clip + "  " + fixed(data.audio_s, 1) + " S";
    cv.text(W - M - text_width(clip, 2), 30, clip, INK, 2);

    // Run numbers.
    std::string model = data.config_name + "   " + data.model_type + "   " + data.decoding + "   " +
                        fixed(data.window_s, 0) + " S WINDOWS";
    cv.text(M, 70, model, INK_2, 2);
    std::string run = "STT " + fixed(data.stt_s, 1) + " S   RTF " +
                      fixed(data.audio_s > 0 ? data.stt_s / data.audio_s : 0.0, 2);
    if (data.peak_rss_kb > 0) run += "   PEAK MEMORY " + fixed(data.peak_rss_kb / 1024.0, 0) + " MIB";
    cv.text(M, 94, run, INK_2, 2);

    // Spectrogram, 0 to 4 kHz, with the STT windows.
    const int sx = M + 40, sy = 124, sw = W - sx - M, sh = 230;
    cv.fill(sx, sy, sw, sh, PANEL);
    auto spec = audio_spectrogram(samples, sample_rate, sw, 512, 4000.0);
    float top = -1e9f;
    for (const auto& col : spec) for (float v : col) top = std::max(top, v);
    for (int c = 0; c < sw && c < static_cast<int>(spec.size()); c++) {
        const auto& col = spec[c];
        for (int y = 0; y < sh; y++) {
            int bin = static_cast<int>((sh - 1 - y) * static_cast<long>(col.size()) / sh);
            cv.set(sx + c, sy + y, ramp((col[bin] - (top - 70.0f)) / 70.0f));
        }
    }
    cv.text(M, sy, "4K", INK_2, 2);
    cv.text(M, sy + sh - 14, "0", INK_2, 2);
    cv.text(M, sy + sh / 2 - 7, "HZ", INK_2, 2);

    const int nwin = static_cast<int>(data.window_hits.size());
    const int hy = sy + sh + 10, hh = 22;
    cv.fill(sx, hy, sw, hh, PANEL);
    for (int i = 0; i < nwin; i++) {
        double t0 = i * data.window_s, t1 = std::min((i + 1) * data.window_s, data.audio_s);
        if (data.window_s <= 0) { t0 = 0; t1 = data.audio_s; }
        int x0 = sx + static_cast<int>(sw * t0 / std::max(data.audio_s, 1e-9));
        int x1 = sx + static_cast<int>(sw * t1 / std::max(data.audio_s, 1e-9));
        if (data.window_hits[i] != WindowMatch::None) {
            cv.fill(x0 + 1, hy + 2, std::max(1, x1 - x0 - 2), hh - 4,
                    data.window_hits[i] == WindowMatch::Exact ? CRITICAL : NEAR);
        }
        if (i > 0) {
            for (int y = sy; y < hy + hh; y += 2) cv.set(x0, y, GRID);
        }
    }
    int lx = cv.text(sx, hy + hh + 8, "STT WINDOWS.", INK_2, 2);
    lx = cv.text(lx, hy + hh + 8, "RED: DISTRESS PHRASE.", CRITICAL, 2);
    cv.text(lx, hy + hh + 8, "ORANGE: NEAR MATCH ONLY.", NEAR, 2);
    std::string axis = fixed(data.audio_s, 0) + " S";
    cv.text(sx + sw - text_width(axis, 2), hy + hh + 8, axis, INK_2, 2);

    // Matched phrases: exact, then approximate with the words that were heard.
    int y = hy + hh + 44;
    auto phrase_line = [&](const std::string& label, const std::vector<PhraseCount>& items, Rgb color) {
        int x = cv.text(M, y, label, INK, 2);
        const int indent = x;
        if (items.empty()) cv.text(x, y, "NONE", INK_2, 2);
        for (const auto& pc : items) {
            std::string item = pc.phrase + " X" + std::to_string(pc.count);
            if (!pc.heard.empty()) {
                item += " (";
                for (std::size_t k = 0; k < pc.heard.size() && k < 3; k++) item += (k ? " " : "") + pc.heard[k];
                item += ")";
            }
            if (x + text_width(item, 2) > W - M) {
                y += 22;
                x = indent;
            }
            x = cv.text(x, y, item, color, 2) + 12;
        }
        y += 26;
    };
    phrase_line("MATCHED:", data.matches.exact, MARK);
    phrase_line("NEAR:   ", data.matches.approximate, NEAR);

    // Transcript excerpt.
    y += 10;
    cv.text(M, y, transcript_is_whole(data.excerpt) ? "TRANSCRIPT" : "TRANSCRIPT EXCERPT", INK, 2);
    y += 26;
    const std::size_t per_line = static_cast<std::size_t>((W - 2 * M) / ((GLYPH_W + 1) * 2));
    for (const auto& line : wrap_words(data.excerpt, per_line)) {
        if (y > H - 50) break;
        int lx = M;
        for (const auto& w : line) lx = cv.text(lx, y, w.text, w.approximate ? NEAR : w.marked ? MARK : INK_2, 2);
        y += 22;
    }

    std::string footer = "OPS-SAT PRETTY ON-BOARD DISTRESS DETECTION  " + data.generated_utc;
    cv.text(M, H - 24, footer, GRID, 2);
    return cv.write_png(png_path);
}
