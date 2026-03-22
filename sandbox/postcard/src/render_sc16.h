/**
 * render_sc16.h - I/Q scatter and spectrogram rendering from sc16 data
 *
 * Requires image.h to be included before this header.
 */
#ifndef RENDER_SC16_H
#define RENDER_SC16_H

#include "doom_palette.h"

#include <cstdint>
#include <cmath>
#include <fstream>
#include <vector>
#include <algorithm>

// Render I/Q scatter from sc16 file as blood splatter effect.
// sc16: interleaved int16 (I, Q, I, Q, ...), 4 bytes per sample.
static void render_iq_scatter(Image& canvas, int ox, int oy, int region_w, int region_h,
                              const char* sc16_path, int max_points = 50000, int dot_size = 1) {
    std::ifstream f(sc16_path, std::ios::binary | std::ios::ate);
    if (!f) {
        fprintf(stderr, "Warning: cannot open %s\n", sc16_path);
        return;
    }

    long long file_size = f.tellg();
    long long num_samples = file_size / 4;
    if (num_samples <= 0) return;
    f.seekg(0);

    long long step = 1;
    if (num_samples > max_points) step = num_samples / max_points;

    std::vector<int16_t> i_vals, q_vals;
    i_vals.reserve(max_points);
    q_vals.reserve(max_points);

    std::vector<int16_t> buf(8192);
    long long sample_idx = 0, next_sample = 0;
    int16_t max_abs = 0;

    while (sample_idx < num_samples && f.good()) {
        long long to_read = std::min((long long)4096, num_samples - sample_idx);
        f.read(reinterpret_cast<char*>(buf.data()), to_read * 4);
        long long got = f.gcount() / 4;
        if (got <= 0) break;

        for (long long j = 0; j < got; j++) {
            if (sample_idx + j == next_sample) {
                int16_t vi = buf[j * 2];
                int16_t vq = buf[j * 2 + 1];
                i_vals.push_back(vi);
                q_vals.push_back(vq);
                int16_t ai = vi < 0 ? -vi : vi;
                int16_t aq = vq < 0 ? -vq : vq;
                if (ai > max_abs) max_abs = ai;
                if (aq > max_abs) max_abs = aq;
                next_sample += step;
            }
        }
        sample_idx += got;
    }

    if (i_vals.empty() || max_abs == 0) return;

    // Aggressive spread: outliers clip out of frame
    float range = max_abs * 0.35f;

    for (size_t k = 0; k < i_vals.size(); k++) {
        float fi = i_vals[k] / range;
        float fq = q_vals[k] / range;

        int px = ox + (int)((fi + 1.0f) * 0.5f * (region_w - 1));
        int py = oy + (int)((1.0f - (fq + 1.0f) * 0.5f) * (region_h - 1));

        int ci = (int)(k * 7 + px * 3 + py * 5) % DOOM_BLOOD_COUNT;
        uint8_t cr = DOOM_BLOOD[ci][0], cg = DOOM_BLOOD[ci][1], cb = DOOM_BLOOD[ci][2];

        int halo = dot_size + dot_size / 2 + 1;
        for (int dy = -halo; dy <= halo; dy++) {
            for (int dx = -halo; dx <= halo; dx++) {
                float dist = std::sqrt((float)(dx*dx + dy*dy));
                if (dist <= dot_size * 0.5f) {
                    canvas.blend_pixel(px + dx, py + dy, cr, cg, cb, 0.5f);
                } else if (dist <= dot_size) {
                    canvas.blend_pixel(px + dx, py + dy, cr, cg, cb, 0.2f);
                } else if (dist <= halo) {
                    float fade = 1.0f - (dist - dot_size) / (halo - dot_size);
                    canvas.blend_pixel(px + dx, py + dy, cr, cg, cb, 0.06f * fade);
                }
            }
        }
    }

    printf("  I/Q scatter: %zu points from %lld samples\n", i_vals.size(), num_samples);
}

// Render spectrogram background with DOOM heat palette and outlier highlights.
static void render_spectrogram_bg(Image& canvas, int ox, int oy, int region_w, int region_h,
                                  const char* sc16_path) {
    std::ifstream f(sc16_path, std::ios::binary | std::ios::ate);
    if (!f) return;

    long long file_size = f.tellg();
    long long num_samples = file_size / 4;
    if (num_samples < 256) return;

    // DFT-based spectrogram: each column is a time window, rows are frequency bins
    int fft_size = 64; // number of frequency bins
    int num_cols = region_w;
    long long hop = std::max(1LL, (num_samples - fft_size) / num_cols);
    const float PI = 3.14159265358979f;

    // Precompute Hamming window
    std::vector<float> hamming(fft_size);
    for (int n = 0; n < fft_size; n++)
        hamming[n] = 0.54f - 0.46f * std::cos(2.0f * PI * n / (fft_size - 1));

    // Precompute twiddle factors
    std::vector<float> cos_table(fft_size * fft_size);
    std::vector<float> sin_table(fft_size * fft_size);
    for (int k = 0; k < fft_size; k++) {
        for (int n = 0; n < fft_size; n++) {
            float angle = -2.0f * PI * k * n / fft_size;
            cos_table[k * fft_size + n] = std::cos(angle);
            sin_table[k * fft_size + n] = std::sin(angle);
        }
    }

    // First pass: stream sc16 data per column, compute DFT magnitudes and find dB range
    std::vector<float> mags(num_cols * fft_size, 0.0f);
    float data_min_db = 0, data_max_db = -200;
    std::vector<int16_t> win_buf(fft_size * 2);

    for (int col = 0; col < num_cols; col++) {
        long long win_start = col * hop;
        if (win_start + fft_size > num_samples) break;

        f.seekg(win_start * 4);
        f.read(reinterpret_cast<char*>(win_buf.data()), fft_size * 4);
        if (f.gcount() < fft_size * 4) break;

        for (int k = 0; k < fft_size; k++) {
            float re = 0, im = 0;
            for (int n = 0; n < fft_size; n++) {
                float vi = win_buf[n * 2] / 32768.0f;
                float vq = win_buf[n * 2 + 1] / 32768.0f;
                float w = hamming[n];
                float cos_a = cos_table[k * fft_size + n];
                float sin_a = sin_table[k * fft_size + n];
                re += w * (vi * cos_a - vq * sin_a);
                im += w * (vi * sin_a + vq * cos_a);
            }
            float mag = std::sqrt(re * re + im * im) / fft_size;
            float db = (mag > 1e-10f) ? 20.0f * std::log10(mag) : -100.0f;
            mags[col * fft_size + k] = db;
            if (db < data_min_db) data_min_db = db;
            if (db > data_max_db) data_max_db = db;
        }
    }
    float db_range = data_max_db - data_min_db;
    if (db_range < 1.0f) db_range = 1.0f;

    // Second pass: render with auto-normalized dB range
    for (int col = 0; col < num_cols; col++) {
        for (int row = 0; row < fft_size; row++) {
            float db = mags[col * fft_size + row];
            float intensity = std::max(0.0f, std::min(1.0f, (db - data_min_db) / db_range));

            uint8_t r, g, b;
            doom_fire_color(intensity, r, g, b);

            int py0 = oy + row * region_h / fft_size;
            int py1 = oy + (row + 1) * region_h / fft_size;
            int px = ox + col;
            if (px >= ox && px < ox + region_w) {
                for (int py = py0; py < py1 && py < oy + region_h; py++) {
                    canvas.blend_pixel(px, py, r, g, b, 0.35f);
                }
            }
        }
    }
}

#endif // RENDER_SC16_H
