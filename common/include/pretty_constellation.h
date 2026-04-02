/*
 * pretty_constellation.h - I/Q constellation BMP generator for sc16 files
 *
 * Header-only. Generates a constellation scatter plot from raw I/Q data
 * for visual triage of SDR captures (e.g. Q channel dropout detection).
 * No external dependencies beyond the standard library.
 */
#ifndef PRETTY_CONSTELLATION_H
#define PRETTY_CONSTELLATION_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "pretty_log.h"
#include "pretty_font.h"

namespace pretty {

struct ConstellationConfig {
    int image_size = 256;           // output image width and height (square)
    int max_points = 50000;         // max samples to plot (uniform subsample)
    float range = 0.0f;             // axis range [-range, +range]; 0 = auto from data
    uint8_t bg_r = 15, bg_g = 10, bg_b = 5;        // near-black warm background
};

// Derive constellation path from sc16 path: /dir/capture.sc16 -> /dir/constellation.bmp
inline std::string make_constellation_filename(const std::string& sc16_path) {
    auto sep = sc16_path.find_last_of("/\\");
    if (sep != std::string::npos) {
        return sc16_path.substr(0, sep + 1) + "constellation.bmp";
    }
    return "constellation.bmp";
}

namespace detail_constellation {

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

} // namespace detail_constellation

// Generate an I/Q constellation BMP from an sc16 file.
// sc16 format: interleaved int16 (I, Q, I, Q, ...), 4 bytes per complex sample.
inline bool generate_constellation(const std::string& sc16_path,
                                   const std::string& output_path,
                                   const ConstellationConfig& cfg = {}) {
    std::ifstream f(sc16_path, std::ios::binary | std::ios::ate);
    if (!f) {
        log_warning() << "Constellation: could not open " << sc16_path << "\n";
        return false;
    }

    long long file_size = f.tellg();
    if (file_size < 4) {
        log_warning() << "Constellation: sc16 file too small (" << file_size << " bytes)\n";
        return false;
    }
    f.seekg(0);

    long long num_samples = file_size / 4;

    // Determine subsample step
    long long step = 1;
    if (cfg.max_points > 0 && num_samples > cfg.max_points) {
        step = num_samples / cfg.max_points;
    }
    long long points_to_read = num_samples / step;

    // Read subsampled I/Q pairs
    std::vector<int16_t> i_vals, q_vals;
    i_vals.reserve(points_to_read);
    q_vals.reserve(points_to_read);

    // Read in chunks for efficiency
    const int chunk_pairs = 4096;
    std::vector<int16_t> buf(chunk_pairs * 2);
    long long sample_idx = 0;
    long long next_sample = 0;

    while (sample_idx < num_samples && f.good()) {
        long long pairs_to_read = std::min((long long)chunk_pairs, num_samples - sample_idx);
        f.read(reinterpret_cast<char*>(buf.data()), pairs_to_read * 4);
        long long got = f.gcount() / 4;
        if (got <= 0) break;

        for (long long j = 0; j < got; j++) {
            if (sample_idx + j == next_sample) {
                i_vals.push_back(buf[j * 2]);
                q_vals.push_back(buf[j * 2 + 1]);
                next_sample += step;
            }
        }
        sample_idx += got;
    }
    f.close();

    if (i_vals.empty()) {
        log_warning() << "Constellation: no samples read\n";
        return false;
    }

    // Determine axis range
    float range = cfg.range;
    if (range <= 0.0f) {
        int16_t max_abs = 0;
        for (size_t k = 0; k < i_vals.size(); k++) {
            int16_t ai = i_vals[k] < 0 ? -i_vals[k] : i_vals[k];
            int16_t aq = q_vals[k] < 0 ? -q_vals[k] : q_vals[k];
            if (ai > max_abs) max_abs = ai;
            if (aq > max_abs) max_abs = aq;
        }
        range = (float)max_abs * 1.1f / 32768.0f;
        if (range < 0.01f) range = 0.01f;
    }

    int size = cfg.image_size;
    std::vector<uint8_t> pixels(size * size * 3);

    // Fill background
    for (int p = 0; p < size * size; p++) {
        pixels[p * 3 + 0] = cfg.bg_r;
        pixels[p * 3 + 1] = cfg.bg_g;
        pixels[p * 3 + 2] = cfg.bg_b;
    }

    // Draw axis crosshairs
    int center = size / 2;
    uint8_t ax_r = 60, ax_g = 10, ax_b = 5;  // subtle dark red axes
    for (int i = 0; i < size; i++) {
        // Horizontal axis (Q=0)
        int ph = (center * size + i) * 3;
        pixels[ph + 0] = ax_r; pixels[ph + 1] = ax_g; pixels[ph + 2] = ax_b;
        // Vertical axis (I=0)
        int pv = (i * size + center) * 3;
        pixels[pv + 0] = ax_r; pixels[pv + 1] = ax_g; pixels[pv + 2] = ax_b;
    }

    // Plot dots with alpha blending
    int half = size / 2;
    for (size_t k = 0; k < i_vals.size(); k++) {
        float fi = (float)i_vals[k] / 32768.0f;
        float fq = (float)q_vals[k] / 32768.0f;

        // Map to pixel coordinates: I = x-axis, Q = y-axis (top = +Q)
        int px = (int)((fi / range + 1.0f) * 0.5f * (size - 1));
        int py = (int)((1.0f - (fq / range + 1.0f) * 0.5f) * (size - 1));

        if (px < 0 || px >= size || py < 0 || py >= size) continue;

        // DOOM fireball: hot center fading to dark edges
        float dx = (float)(px - half) / half;
        float dy = (float)(py - half) / half;
        float dist = std::sqrt(dx * dx + dy * dy);
        if (dist > 1.0f) dist = 1.0f;

        uint8_t dr, dg, db;
        if (dist < 0.15f) {
            float t = dist / 0.15f;
            dr = 255; dg = (uint8_t)(255 - t * 30); db = (uint8_t)(200 - t * 120);
        } else if (dist < 0.3f) {
            float t = (dist - 0.15f) / 0.15f;
            dr = 255; dg = (uint8_t)(225 - t * 80); db = (uint8_t)(80 - t * 60);
        } else if (dist < 0.5f) {
            float t = (dist - 0.3f) / 0.2f;
            dr = (uint8_t)(255 - t * 30); dg = (uint8_t)(145 - t * 90); db = (uint8_t)(20 - t * 15);
        } else if (dist < 0.7f) {
            float t = (dist - 0.5f) / 0.2f;
            dr = (uint8_t)(225 - t * 80); dg = (uint8_t)(55 - t * 40); db = 5;
        } else {
            float t = (dist - 0.7f) / 0.3f;
            dr = (uint8_t)(145 - t * 80); dg = (uint8_t)(15 - t * 10); db = (uint8_t)(5 - t * 3);
        }

        int idx = (py * size + px) * 3;
        pixels[idx + 0] = dr;
        pixels[idx + 1] = dg;
        pixels[idx + 2] = db;
    }

    // Axis labels: I (bottom-right), Q (top-left)
    draw_text(pixels, size, size, size - GLYPH_W - 2, center + 3, "I", 120, 120, 120);
    draw_text(pixels, size, size, center + 3, 2, "Q", 120, 120, 120);

    if (!detail_constellation::write_bmp(output_path, pixels, size, size)) {
        log_warning() << "Constellation: failed to write " << output_path << "\n";
        return false;
    }

    long long bmp_size = 54LL + ((size * 3 + 3) & ~3) * (long long)size;
    log_info() << "Constellation: " << size << "x" << size
               << " (" << (bmp_size / 1024) << " KB), "
               << i_vals.size() << " points plotted\n";

    return true;
}

} // namespace pretty

#endif // PRETTY_CONSTELLATION_H
