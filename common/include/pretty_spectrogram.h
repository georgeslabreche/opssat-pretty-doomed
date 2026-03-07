/*
 * pretty_spectrogram.h - Spectrogram BMP generator for sc16 I/Q files
 *
 * Header-only. Generates a spectrogram thumbnail from raw I/Q data
 * for visual triage of SDR captures. Requires FFTW3 (-lfftw3f).
 */
#ifndef PRETTY_SPECTROGRAM_H
#define PRETTY_SPECTROGRAM_H

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

    if (!detail::write_bmp(output_path, pixels, image_width, image_height)) {
        log_warning() << "Spectrogram: failed to write " << output_path << "\n";
        return false;
    }

    double duration = (double)num_samples / sample_rate;
    double freq_res = (double)sample_rate / fft_size;
    long long bmp_size = 54LL + ((image_width * 3 + 3) & ~3) * (long long)image_height;
    log_info() << "Spectrogram: " << image_width << "x" << image_height
               << " (" << (bmp_size / 1024) << " KB), "
               << duration << "s, freq res " << freq_res << " Hz\n";

    return true;
}

} // namespace pretty

#endif // PRETTY_SPECTROGRAM_H
