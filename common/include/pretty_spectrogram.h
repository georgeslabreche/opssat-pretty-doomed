/*
 * pretty_spectrogram.h - Spectrogram BMP generator for sc16 I/Q files
 *
 * Header-only. Generates a spectrogram thumbnail from raw I/Q data
 * for visual triage of SDR captures. Requires FFTW3 (-lfftw3f).
 */
#ifndef PRETTY_SPECTROGRAM_H
#define PRETTY_SPECTROGRAM_H

#include "pretty_font.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include <fftw3.h>

#include "pretty_log.h"

namespace pretty {

struct SpectrogramConfig {
    int fft_size = 256;        // FFT bins (= image height)
    int image_width = 1024;    // time axis pixels
    float min_db = -80.0f;     // colormap floor (dB)
    float max_db = 0.0f;       // colormap ceiling (dB)
};

// Derive spectrogram path from sc16 path: /dir/capture.sc16 -> /dir/spectrogram.bmp
inline std::string make_spectrogram_filename(const std::string& sc16_path) {
    auto sep = sc16_path.find_last_of("/\\");
    if (sep != std::string::npos) {
        return sc16_path.substr(0, sep + 1) + "spectrogram.bmp";
    }
    return "spectrogram.bmp";
}

namespace detail {

// Colormap: black -> blue -> cyan -> yellow -> red
inline void db_to_rgb(float t, uint8_t& r, uint8_t& g, uint8_t& b) {
    if (t <= 0.0f) { r = 0; g = 0; b = 0; return; }
    if (t >= 1.0f) { r = 255; g = 0; b = 0; return; }

    float scaled = t * 4.0f;
    int seg = (int)scaled;
    float frac = scaled - seg;

    switch (seg) {
        case 0: // black -> blue
            r = 0; g = 0; b = (uint8_t)(frac * 255);
            break;
        case 1: // blue -> cyan
            r = 0; g = (uint8_t)(frac * 255); b = 255;
            break;
        case 2: // cyan -> yellow
            r = (uint8_t)(frac * 255);
            g = 255;
            b = (uint8_t)((1.0f - frac) * 255);
            break;
        case 3: // yellow -> red
            r = 255; g = (uint8_t)((1.0f - frac) * 255); b = 0;
            break;
        default:
            r = 255; g = 0; b = 0;
            break;
    }
}

inline bool write_bmp(const std::string& path, const std::vector<uint8_t>& pixels,
                      int width, int height) {
    int row_stride = width * 3;
    int row_padded = (row_stride + 3) & ~3;
    int pixel_data_size = row_padded * height;
    int file_size = 14 + 40 + pixel_data_size;

    std::ofstream f(path, std::ios::binary);
    if (!f) return false;

    // File header (14 bytes)
    uint8_t fh[14] = {};
    fh[0] = 'B'; fh[1] = 'M';
    std::memcpy(&fh[2], &file_size, 4);
    int data_offset = 54;
    std::memcpy(&fh[10], &data_offset, 4);
    f.write(reinterpret_cast<char*>(fh), 14);

    // Info header (BITMAPINFOHEADER, 40 bytes)
    uint8_t ih[40] = {};
    int info_size = 40;
    std::memcpy(&ih[0], &info_size, 4);
    std::memcpy(&ih[4], &width, 4);
    std::memcpy(&ih[8], &height, 4);
    uint16_t planes = 1;
    std::memcpy(&ih[12], &planes, 2);
    uint16_t bpp = 24;
    std::memcpy(&ih[14], &bpp, 2);
    std::memcpy(&ih[20], &pixel_data_size, 4);
    f.write(reinterpret_cast<char*>(ih), 40);

    // Pixel data (BMP is bottom-up, BGR byte order)
    std::vector<uint8_t> row_buf(row_padded, 0);
    for (int y = height - 1; y >= 0; y--) {
        for (int x = 0; x < width; x++) {
            int src = (y * width + x) * 3;
            row_buf[x * 3 + 0] = pixels[src + 2]; // B
            row_buf[x * 3 + 1] = pixels[src + 1]; // G
            row_buf[x * 3 + 2] = pixels[src + 0]; // R
        }
        f.write(reinterpret_cast<char*>(row_buf.data()), row_padded);
    }

    return f.good();
}

} // namespace detail

// Generate a spectrogram BMP from an sc16 I/Q file.
// sc16 format: interleaved int16 (I, Q, I, Q, ...), 4 bytes per complex sample.
// sample_rate: effective sample rate of the I/Q data (Hz).
inline bool generate_spectrogram(const std::string& sc16_path,
                                 const std::string& output_path,
                                 long long sample_rate,
                                 const SpectrogramConfig& cfg = {}) {
    std::ifstream f(sc16_path, std::ios::binary | std::ios::ate);
    if (!f) {
        log_warning() << "Spectrogram: could not open " << sc16_path << "\n";
        return false;
    }

    long long file_size = f.tellg();
    if (file_size < 4) {
        log_warning() << "Spectrogram: sc16 file too small (" << file_size << " bytes)\n";
        return false;
    }
    f.seekg(0);

    long long num_samples = file_size / 4;
    std::vector<int16_t> raw(num_samples * 2);
    f.read(reinterpret_cast<char*>(raw.data()), num_samples * 4);
    f.close();

    int fft_size = cfg.fft_size;
    int image_width = cfg.image_width;
    int image_height = fft_size;

    if (num_samples < fft_size) {
        log_warning() << "Spectrogram: not enough samples (" << num_samples
                      << ") for FFT size " << fft_size << "\n";
        return false;
    }

    // Hop size: spread FFT windows evenly across the file
    long long hop = (num_samples - fft_size) / (image_width - 1);
    if (hop < 1) hop = 1;
    // Adjust width if file is shorter than requested
    int actual_width = (int)((num_samples - fft_size) / hop) + 1;
    if (actual_width < image_width) image_width = actual_width;
    if (image_width < 1) image_width = 1;

    // FFTW setup
    fftwf_complex* fft_in  = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * fft_size);
    fftwf_complex* fft_out = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * fft_size);
    if (!fft_in || !fft_out) {
        if (fft_in)  fftwf_free(fft_in);
        if (fft_out) fftwf_free(fft_out);
        log_warning() << "Spectrogram: FFTW malloc failed\n";
        return false;
    }
    fftwf_plan plan = fftwf_plan_dft_1d(fft_size, fft_in, fft_out, FFTW_FORWARD, FFTW_ESTIMATE);
    if (!plan) {
        fftwf_free(fft_in);
        fftwf_free(fft_out);
        log_warning() << "Spectrogram: FFTW plan creation failed\n";
        return false;
    }

    // Hann window
    std::vector<float> window(fft_size);
    for (int i = 0; i < fft_size; i++) {
        window[i] = 0.5f * (1.0f - std::cos(2.0f * (float)M_PI * i / (fft_size - 1)));
    }

    float db_range = cfg.max_db - cfg.min_db;
    if (db_range <= 0) db_range = 80.0f;
    int half_fft = fft_size / 2;

    // Compute power spectrum for each time column
    std::vector<uint8_t> pixels(image_width * image_height * 3);

    for (int col = 0; col < image_width; col++) {
        long long offset = col * hop;
        if (offset + fft_size > num_samples) offset = num_samples - fft_size;

        // Fill FFT input with windowed I/Q samples
        for (int i = 0; i < fft_size; i++) {
            long long idx = (offset + i) * 2;
            fft_in[i][0] = raw[idx]     * window[i] / 32768.0f;  // I
            fft_in[i][1] = raw[idx + 1] * window[i] / 32768.0f;  // Q
        }

        fftwf_execute(plan);

        // FFT shift: map row 0 = -fs/2, row fft_size-1 = +fs/2
        for (int row = 0; row < image_height; row++) {
            int bin = (row + half_fft) % fft_size;
            float re = fft_out[bin][0];
            float im = fft_out[bin][1];
            float mag_sq = re * re + im * im;
            float db = 10.0f * std::log10(mag_sq + 1e-20f);

            float normalized = (db - cfg.min_db) / db_range;
            if (normalized < 0.0f) normalized = 0.0f;
            if (normalized > 1.0f) normalized = 1.0f;

            uint8_t r, g, b;
            detail::db_to_rgb(normalized, r, g, b);

            int px = (row * image_width + col) * 3;
            pixels[px + 0] = r;
            pixels[px + 1] = g;
            pixels[px + 2] = b;
        }
    }

    fftwf_destroy_plan(plan);
    fftwf_free(fft_in);
    fftwf_free(fft_out);

    double duration = (double)num_samples / sample_rate;
    double freq_res = (double)sample_rate / fft_size;

    // Overlay axis labels on the spectrogram (with dark background for contrast)
    // Time labels along bottom edge
    for (int i = 0; i <= 4; i++) {
        float t = (float)i / 4.0f * duration;
        int x = (int)((float)i / 4.0f * (image_width - 1));
        std::string label = fmt_float(t, 1) + "S";
        int lw = text_width(label);
        draw_text_bg(pixels, image_width, image_height,
                     x - lw / 2, image_height - GLYPH_H - 2,
                     label, 255, 255, 255);
    }
    // Frequency labels along left edge
    for (int i = 0; i <= 4; i++) {
        float f = ((float)i / 4.0f - 0.5f) * sample_rate;
        int y = image_height - 1 - (int)((float)i / 4.0f * (image_height - 1));
        std::string label = fmt_float(f / 1000.0f, 0) + "K";
        draw_text_bg(pixels, image_width, image_height,
                     2, y - GLYPH_H / 2, label, 255, 255, 255);
    }

    if (!detail::write_bmp(output_path, pixels, image_width, image_height)) {
        log_warning() << "Spectrogram: failed to write " << output_path << "\n";
        return false;
    }
    long long bmp_size = 54LL + ((image_width * 3 + 3) & ~3) * (long long)image_height;
    log_info() << "Spectrogram: " << image_width << "x" << image_height
               << " (" << (bmp_size / 1024) << " KB), "
               << duration << "s, freq res " << freq_res << " Hz\n";

    return true;
}

// Derive PSD CSV/BMP paths from sc16 path
inline std::string make_psd_csv_filename(const std::string& sc16_path) {
    auto sep = sc16_path.find_last_of("/\\");
    if (sep != std::string::npos)
        return sc16_path.substr(0, sep + 1) + "capture-psd.csv";
    return "capture-psd.csv";
}

inline std::string make_psd_bmp_filename(const std::string& sc16_path) {
    auto sep = sc16_path.find_last_of("/\\");
    if (sep != std::string::npos)
        return sc16_path.substr(0, sep + 1) + "capture-psd.bmp";
    return "capture-psd.bmp";
}

// PSD configuration
struct PsdConfig {
    int fft_size = 1024;           // FFT bins for Welch averaging
    float min_db = -80.0f;
    float max_db = 0.0f;
    int bmp_width = 800;
    int bmp_height = 256;
};

// Generate a PSD (Power Spectral Density) BMP + CSV from an sc16 I/Q file.
// Uses Welch's method: overlapping Hann-windowed FFT segments averaged.
// CSV: frequency_hz,power_db_hz (one row per bin, used for cross-capture comparison).
// BMP: 1D frequency vs power line plot.
inline bool generate_psd(const std::string& sc16_path,
                         const std::string& csv_path,
                         const std::string& bmp_path,
                         long long sample_rate,
                         const PsdConfig& cfg = {}) {
    std::ifstream f(sc16_path, std::ios::binary | std::ios::ate);
    if (!f) {
        log_warning() << "PSD: could not open " << sc16_path << "\n";
        return false;
    }

    long long file_size = f.tellg();
    if (file_size < 4) {
        log_warning() << "PSD: sc16 file too small\n";
        return false;
    }
    f.seekg(0);

    long long num_samples = file_size / 4;
    std::vector<int16_t> raw(num_samples * 2);
    f.read(reinterpret_cast<char*>(raw.data()), num_samples * 4);
    f.close();

    int fft_size = cfg.fft_size;
    if (num_samples < fft_size) {
        log_warning() << "PSD: not enough samples (" << num_samples
                      << ") for FFT size " << fft_size << "\n";
        return false;
    }

    // Hann window + coherent gain for normalization
    std::vector<float> window(fft_size);
    double window_sumsq = 0;
    for (int i = 0; i < fft_size; i++) {
        window[i] = 0.5f * (1.0f - std::cos(2.0f * (float)M_PI * i / (fft_size - 1)));
        window_sumsq += (double)window[i] * window[i];
    }

    // FFTW setup
    fftwf_complex* fft_in  = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * fft_size);
    fftwf_complex* fft_out = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * fft_size);
    if (!fft_in || !fft_out) {
        if (fft_in)  fftwf_free(fft_in);
        if (fft_out) fftwf_free(fft_out);
        log_warning() << "PSD: FFTW malloc failed\n";
        return false;
    }
    fftwf_plan plan = fftwf_plan_dft_1d(fft_size, fft_in, fft_out, FFTW_FORWARD, FFTW_ESTIMATE);
    if (!plan) {
        fftwf_free(fft_in);
        fftwf_free(fft_out);
        log_warning() << "PSD: FFTW plan creation failed\n";
        return false;
    }

    // Welch averaging: 50% overlap segments
    long long hop = fft_size / 2;
    int num_segments = (int)((num_samples - fft_size) / hop) + 1;
    if (num_segments < 1) num_segments = 1;

    std::vector<double> psd(fft_size, 0.0);

    for (int seg = 0; seg < num_segments; seg++) {
        long long offset = seg * hop;
        if (offset + fft_size > num_samples) break;

        for (int i = 0; i < fft_size; i++) {
            long long idx = (offset + i) * 2;
            fft_in[i][0] = raw[idx]     * window[i] / 32768.0f;
            fft_in[i][1] = raw[idx + 1] * window[i] / 32768.0f;
        }
        fftwf_execute(plan);

        for (int i = 0; i < fft_size; i++) {
            float re = fft_out[i][0];
            float im = fft_out[i][1];
            psd[i] += (double)(re * re + im * im);
        }
    }

    fftwf_destroy_plan(plan);
    fftwf_free(fft_in);
    fftwf_free(fft_out);

    // Normalize: average over segments, normalize by window power and sample rate
    double norm = num_segments * window_sumsq * sample_rate;
    int half_fft = fft_size / 2;
    std::vector<float> freq(fft_size);
    std::vector<float> power_db(fft_size);
    float db_min = 1e30f, db_max = -1e30f;

    for (int i = 0; i < fft_size; i++) {
        // FFT shift: map bin 0 = -fs/2
        int bin = (i + half_fft) % fft_size;
        freq[i] = (float)(i - half_fft) * sample_rate / fft_size;
        float db = (float)(10.0 * std::log10(psd[bin] / norm + 1e-20));
        power_db[i] = db;
        if (db < db_min) db_min = db;
        if (db > db_max) db_max = db;
    }

    // Write CSV
    {
        std::ofstream csv(csv_path);
        if (!csv) {
            log_warning() << "PSD: could not write " << csv_path << "\n";
            return false;
        }
        csv << "frequency_hz,power_db_hz\n";
        for (int i = 0; i < fft_size; i++) {
            csv << freq[i] << "," << power_db[i] << "\n";
        }
    }

    // Write BMP: line plot with axis labels
    const int margin_left = 60;
    const int margin_bottom = 20;
    const int margin_top = 4;
    const int margin_right = 4;
    int plot_w = cfg.bmp_width - margin_left - margin_right;
    int plot_h = cfg.bmp_height - margin_bottom - margin_top;
    int w = cfg.bmp_width;
    int h = cfg.bmp_height;

    float plot_range = db_max - db_min;
    if (plot_range < 10.0f) plot_range = 10.0f;
    float plot_min = db_min - plot_range * 0.05f;
    float plot_max = db_max + plot_range * 0.05f;
    plot_range = plot_max - plot_min;

    float freq_min_f = freq[0];
    float freq_max_f = freq[fft_size - 1];

    std::vector<uint8_t> pixels(w * h * 3, 0);

    // Grid lines + y-axis tick labels (dB)
    int num_y_ticks = 5;
    for (int i = 0; i <= num_y_ticks; i++) {
        float db_val = plot_min + (float)i / num_y_ticks * plot_range;
        int y = margin_top + plot_h - (int)((float)i / num_y_ticks * plot_h);
        for (int x = margin_left; x < margin_left + plot_w; x++) {
            int px = (y * w + x) * 3;
            pixels[px] = pixels[px + 1] = pixels[px + 2] = 40;
        }
        std::string label = fmt_float(db_val, 0);
        int lw = text_width(label);
        draw_text(pixels, w, h, margin_left - lw - 3, y - GLYPH_H / 2,
                  label, 160, 160, 160);
    }

    // x-axis tick labels (frequency in kHz)
    int num_x_ticks = 5;
    for (int i = 0; i <= num_x_ticks; i++) {
        float freq_val = freq_min_f + (float)i / num_x_ticks * (freq_max_f - freq_min_f);
        int x = margin_left + (int)((float)i / num_x_ticks * plot_w);
        for (int y = margin_top; y < margin_top + plot_h; y++) {
            int px = (y * w + x) * 3;
            pixels[px] = pixels[px + 1] = pixels[px + 2] = 40;
        }
        std::string label = fmt_float(freq_val / 1000.0f, 0) + "K";
        int lw = text_width(label);
        draw_text(pixels, w, h, x - lw / 2, margin_top + plot_h + 4,
                  label, 160, 160, 160);
    }

    // PSD curve (cyan)
    int prev_py = -1;
    for (int px = 0; px < plot_w; px++) {
        int bin = px * fft_size / plot_w;
        if (bin >= fft_size) bin = fft_size - 1;
        float norm_val = (power_db[bin] - plot_min) / plot_range;
        if (norm_val < 0) norm_val = 0;
        if (norm_val > 1) norm_val = 1;
        int py = margin_top + (int)((1.0f - norm_val) * (plot_h - 1));
        int ix = margin_left + px;

        if (prev_py >= 0) {
            int y0 = std::min(prev_py, py);
            int y1 = std::max(prev_py, py);
            for (int yy = y0; yy <= y1; yy++) {
                int idx = (yy * w + ix) * 3;
                pixels[idx] = 0; pixels[idx + 1] = 220; pixels[idx + 2] = 220;
            }
        }
        int idx = (py * w + ix) * 3;
        pixels[idx] = 0; pixels[idx + 1] = 255; pixels[idx + 2] = 255;
        prev_py = py;
    }

    if (!detail::write_bmp(bmp_path, pixels, w, h)) {
        log_warning() << "PSD: failed to write " << bmp_path << "\n";
        return false;
    }

    double freq_res = (double)sample_rate / fft_size;
    log_info() << "PSD: " << w << "x" << h << ", " << fft_size
               << " bins, freq res " << freq_res << " Hz, "
               << num_segments << " segments\n";

    return true;
}

// Generate a cross-capture PSD comparison BMP from per-capture CSV files.
// Overlays each capture's PSD curve on a single plot with distinct colors.
inline bool generate_psd_comparison(
    const std::vector<std::string>& psd_csv_paths,
    const std::string& output_path,
    int image_width = 800,
    int image_height = 512) {

    if (psd_csv_paths.empty()) return false;

    // Color palette for up to 8 captures
    static const uint8_t colors[][3] = {
        {0, 255, 255},   // cyan
        {255, 100, 100},  // red
        {100, 255, 100},  // green
        {255, 200, 50},   // yellow
        {200, 100, 255},  // purple
        {255, 150, 50},   // orange
        {100, 200, 255},  // light blue
        {255, 100, 200},  // pink
    };
    int num_colors = sizeof(colors) / sizeof(colors[0]);

    // Read all CSVs
    struct PsdData {
        std::vector<float> freq;
        std::vector<float> power;
    };
    std::vector<PsdData> all_data;
    float global_db_min = 1e30f, global_db_max = -1e30f;
    float global_freq_min = 1e30f, global_freq_max = -1e30f;

    for (const auto& path : psd_csv_paths) {
        PsdData d;
        std::ifstream csv(path);
        if (!csv) continue;
        std::string line;
        std::getline(csv, line); // skip header
        while (std::getline(csv, line)) {
            auto comma = line.find(',');
            if (comma == std::string::npos) continue;
            float fr = std::stof(line.substr(0, comma));
            float pw = std::stof(line.substr(comma + 1));
            d.freq.push_back(fr);
            d.power.push_back(pw);
            if (pw < global_db_min) global_db_min = pw;
            if (pw > global_db_max) global_db_max = pw;
            if (fr < global_freq_min) global_freq_min = fr;
            if (fr > global_freq_max) global_freq_max = fr;
        }
        if (!d.freq.empty()) all_data.push_back(std::move(d));
    }

    if (all_data.empty()) return false;

    float plot_range = global_db_max - global_db_min;
    if (plot_range < 10) plot_range = 10;
    float plot_min = global_db_min - plot_range * 0.05f;
    float plot_max = global_db_max + plot_range * 0.05f;
    plot_range = plot_max - plot_min;

    float freq_range = global_freq_max - global_freq_min;
    if (freq_range <= 0) freq_range = 1;

    const int margin_left = 60;
    const int margin_bottom = 20;
    const int margin_top = 14;
    const int margin_right = 4;
    int w = image_width;
    int h = image_height;
    int plot_w = w - margin_left - margin_right;
    int plot_h = h - margin_bottom - margin_top;

    std::vector<uint8_t> pixels(w * h * 3, 0);

    // Grid + y-axis labels (dB)
    int num_y_ticks = 5;
    for (int i = 0; i <= num_y_ticks; i++) {
        float db_val = plot_min + (float)i / num_y_ticks * plot_range;
        int y = margin_top + plot_h - (int)((float)i / num_y_ticks * plot_h);
        for (int x = margin_left; x < margin_left + plot_w; x++) {
            int px = (y * w + x) * 3;
            pixels[px] = pixels[px + 1] = pixels[px + 2] = 40;
        }
        std::string label = fmt_float(db_val, 0);
        int lw = text_width(label);
        draw_text(pixels, w, h, margin_left - lw - 3, y - GLYPH_H / 2,
                  label, 160, 160, 160);
    }

    // x-axis labels (frequency in kHz)
    int num_x_ticks = 5;
    for (int i = 0; i <= num_x_ticks; i++) {
        float freq_val = global_freq_min + (float)i / num_x_ticks * freq_range;
        int x = margin_left + (int)((float)i / num_x_ticks * plot_w);
        for (int y = margin_top; y < margin_top + plot_h; y++) {
            int px = (y * w + x) * 3;
            pixels[px] = pixels[px + 1] = pixels[px + 2] = 40;
        }
        std::string label = fmt_float(freq_val / 1000.0f, 0) + "K";
        int lw = text_width(label);
        draw_text(pixels, w, h, x - lw / 2, margin_top + plot_h + 4,
                  label, 160, 160, 160);
    }

    // Draw each capture's PSD curve
    for (size_t c = 0; c < all_data.size(); c++) {
        const auto& d = all_data[c];
        const uint8_t* color = colors[c % num_colors];
        int prev_py = -1;

        for (int px = 0; px < plot_w; px++) {
            float target_freq = global_freq_min + (float)px / plot_w * freq_range;
            int nearest = 0;
            float best_dist = 1e30f;
            for (size_t i = 0; i < d.freq.size(); i++) {
                float dist = std::fabs(d.freq[i] - target_freq);
                if (dist < best_dist) { best_dist = dist; nearest = (int)i; }
            }
            float norm_val = (d.power[nearest] - plot_min) / plot_range;
            if (norm_val < 0) norm_val = 0;
            if (norm_val > 1) norm_val = 1;
            int py = margin_top + (int)((1.0f - norm_val) * (plot_h - 1));
            int ix = margin_left + px;

            if (prev_py >= 0) {
                int y0 = std::min(prev_py, py);
                int y1 = std::max(prev_py, py);
                for (int yy = y0; yy <= y1; yy++) {
                    int idx = (yy * w + ix) * 3;
                    pixels[idx] = color[0]; pixels[idx + 1] = color[1]; pixels[idx + 2] = color[2];
                }
            }
            int idx = (py * w + ix) * 3;
            pixels[idx] = color[0]; pixels[idx + 1] = color[1]; pixels[idx + 2] = color[2];
            prev_py = py;
        }

        // Legend label
        int legend_y = margin_top + 4 + (int)c * 12;
        int legend_x = margin_left + 8;
        // Color swatch
        for (int dy = 0; dy < 7; dy++) {
            for (int dx = 0; dx < 12; dx++) {
                int idx = ((legend_y + dy) * w + legend_x + dx) * 3;
                if (legend_y + dy < h && legend_x + dx < w) {
                    pixels[idx] = color[0]; pixels[idx + 1] = color[1]; pixels[idx + 2] = color[2];
                }
            }
        }
        char label_buf[16];
        std::snprintf(label_buf, sizeof(label_buf), "CAP %d", (int)(c + 1));
        draw_text(pixels, w, h, legend_x + 16, legend_y,
                  label_buf, color[0], color[1], color[2]);
    }

    if (!detail::write_bmp(output_path, pixels, w, h)) {
        log_warning() << "PSD comparison: failed to write " << output_path << "\n";
        return false;
    }

    log_info() << "PSD comparison: " << all_data.size() << " captures, "
               << w << "x" << h << "\n";
    return true;
}

} // namespace pretty

#endif // PRETTY_SPECTROGRAM_H
