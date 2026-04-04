/**
 * postcard_sc16.h - I/Q scatter and spectrogram rendering from sc16 data
 *
 * Requires postcard_image.h to be included before this header.
 */
#ifndef POSTCARD_SC16_H
#define POSTCARD_SC16_H

#include "doom_palette.h"

#include <cstdint>
#include <cmath>
#include <fstream>
#include <vector>
#include <algorithm>
#ifdef POSTCARD_USE_FFTW
#include <fftw3.h>
#endif

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

    // Aggressive spread (0.35) zooms into the scatter for a wide blood
    // splatter effect. For weak signals (low max_abs), this clips most
    // points to the plot edges and the few distinct int16 values create
    // a visible quantization grid. Fall back to 1.1 (10% margin, no
    // clipping) when the signal is too weak for the aggressive spread.
    // Dither: ±0.5 LSB jitter smooths the quantization grid in both cases.
    float range = (max_abs < 200) ? max_abs * 1.1f : max_abs * 0.35f;

    // Boost blend alpha for weak signals. Uniform noise distributes points
    // evenly (no clustering), so per-pixel density is low. Strong signals
    // cluster naturally on constellation points, building up intensity.
    float alpha_boost = std::min(2.0f, std::max(1.0f, 500.0f / (float)max_abs));

    for (size_t k = 0; k < i_vals.size(); k++) {
        float di = ((rand() & 0xFF) / 255.0f) - 0.5f;
        float dq = ((rand() & 0xFF) / 255.0f) - 0.5f;
        float fi = (i_vals[k] + di) / range;
        float fq = (q_vals[k] + dq) / range;

        int px = ox + (int)((fi + 1.0f) * 0.5f * (region_w - 1));
        int py = oy + (int)((1.0f - (fq + 1.0f) * 0.5f) * (region_h - 1));

        int ci = (int)(k * 7 + px * 3 + py * 5) % DOOM_BLOOD_COUNT;
        uint8_t cr = DOOM_BLOOD[ci][0], cg = DOOM_BLOOD[ci][1], cb = DOOM_BLOOD[ci][2];

        int halo = dot_size + dot_size / 2 + 1;
        for (int dy = -halo; dy <= halo; dy++) {
            for (int dx = -halo; dx <= halo; dx++) {
                float dist = std::sqrt((float)(dx*dx + dy*dy));
                if (dist <= dot_size * 0.5f) {
                    canvas.blend_pixel(px + dx, py + dy, cr, cg, cb,
                                       std::min(1.0f, 0.5f * alpha_boost));
                } else if (dist <= dot_size) {
                    canvas.blend_pixel(px + dx, py + dy, cr, cg, cb,
                                       std::min(1.0f, 0.2f * alpha_boost));
                } else if (dist <= halo) {
                    float fade = 1.0f - (dist - dot_size) / (halo - dot_size);
                    canvas.blend_pixel(px + dx, py + dy, cr, cg, cb,
                                       std::min(1.0f, 0.06f * fade * alpha_boost));
                }
            }
        }
    }

    printf("  I/Q scatter: %zu points from %lld samples\n", i_vals.size(), num_samples);
}

// Render spectrogram background onto canvas.
// When FFTW is available: 256-bin FFT with Hann window and FFT shift.
// Without FFTW: simple magnitude-based rendering (no frequency decomposition).
static void render_spectrogram_bg(Image& canvas, int ox, int oy, int region_w, int region_h,
                                  const char* sc16_path) {
    std::ifstream f(sc16_path, std::ios::binary | std::ios::ate);
    if (!f) return;

    long long file_size = f.tellg();
    long long num_samples = file_size / 4;
    if (num_samples < 256) return;

    int num_cols = region_w;

#ifdef POSTCARD_USE_FFTW
    int fft_size = 256;
    if (num_samples < fft_size) return;

    long long hop = (num_samples - fft_size) / (num_cols - 1);
    if (hop < 1) hop = 1;
    int actual_cols = (int)((num_samples - fft_size) / hop) + 1;
    if (actual_cols < num_cols) num_cols = actual_cols;
    if (num_cols < 1) num_cols = 1;

    fftwf_complex* fft_in  = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * fft_size);
    fftwf_complex* fft_out = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * fft_size);
    if (!fft_in || !fft_out) {
        if (fft_in)  fftwf_free(fft_in);
        if (fft_out) fftwf_free(fft_out);
        return;
    }
    fftwf_plan plan = fftwf_plan_dft_1d(fft_size, fft_in, fft_out, FFTW_FORWARD, FFTW_ESTIMATE);
    if (!plan) {
        fftwf_free(fft_in);
        fftwf_free(fft_out);
        return;
    }

    std::vector<float> window(fft_size);
    for (int i = 0; i < fft_size; i++)
        window[i] = 0.5f * (1.0f - std::cos(2.0f * (float)M_PI * i / (fft_size - 1)));

    int half_fft = fft_size / 2;
    int num_rows = fft_size;

    // Small per-column read buffer (stream instead of loading entire file)
    std::vector<int16_t> col_buf(fft_size * 2);
#else
    f.seekg(0);

    // Non-FFTW fallback loads entire file (small num_rows, simple magnitude)
    std::vector<int16_t> raw(num_samples * 2);
    f.read(reinterpret_cast<char*>(raw.data()), num_samples * 4);
    f.close();

    // Fallback: simple magnitude spectrogram (no frequency decomposition)
    int num_rows = 64;
    long long samples_per_col = num_samples / num_cols;
    int samples_per_row = std::max(1, (int)(samples_per_col / num_rows));
#endif

    std::vector<float> mags(num_cols * num_rows, 0.0f);
    float data_min_db = 0, data_max_db = -200;

    for (int col = 0; col < num_cols; col++) {
#ifdef POSTCARD_USE_FFTW
        long long offset = col * hop;
        if (offset + fft_size > num_samples) offset = num_samples - fft_size;

        // Seek and read only fft_size samples for this column
        f.seekg(offset * 4);
        f.read(reinterpret_cast<char*>(col_buf.data()), fft_size * 4);

        for (int i = 0; i < fft_size; i++) {
            fft_in[i][0] = col_buf[i * 2]     * window[i] / 32768.0f;
            fft_in[i][1] = col_buf[i * 2 + 1] * window[i] / 32768.0f;
        }
        fftwf_execute(plan);

        for (int row = 0; row < num_rows; row++) {
            int bin = (row + half_fft) % fft_size;
            float re = fft_out[bin][0];
            float im = fft_out[bin][1];
            float mag_sq = re * re + im * im;
            float db = 10.0f * std::log10(mag_sq + 1e-20f);
            mags[col * num_rows + row] = db;
            if (db < data_min_db) data_min_db = db;
            if (db > data_max_db) data_max_db = db;
        }
#else
        long long col_offset = col * samples_per_col;
        for (int row = 0; row < num_rows; row++) {
            long long block_start = col_offset + row * samples_per_row;
            float avg_mag = 0;
            int count = 0;
            for (int s = 0; s < samples_per_row && block_start + s < num_samples; s++) {
                long long idx = block_start + s;
                float vi = raw[idx * 2] / 32768.0f;
                float vq = raw[idx * 2 + 1] / 32768.0f;
                avg_mag += std::sqrt(vi * vi + vq * vq);
                count++;
            }
            if (count > 0) avg_mag /= count;
            float db = (avg_mag > 1e-10f) ? 20.0f * std::log10(avg_mag) : -100.0f;
            mags[col * num_rows + row] = db;
            if (db < data_min_db) data_min_db = db;
            if (db > data_max_db) data_max_db = db;
        }
#endif
    }

#ifdef POSTCARD_USE_FFTW
    f.close();
    fftwf_destroy_plan(plan);
    fftwf_free(fft_in);
    fftwf_free(fft_out);
#endif

    float db_range = data_max_db - data_min_db;
    if (db_range < 1.0f) db_range = 1.0f;

    // Second pass: render with DOOM fire palette
    for (int col = 0; col < num_cols; col++) {
        for (int row = 0; row < num_rows; row++) {
            float db = mags[col * num_rows + row];
            float intensity = std::max(0.0f, std::min(1.0f, (db - data_min_db) / db_range));

            uint8_t r, g, b;
            doom_fire_color(intensity, r, g, b);

            int py0 = oy + row * region_h / num_rows;
            int py1 = oy + (row + 1) * region_h / num_rows;
            int px = ox + col;
            if (px >= ox && px < ox + region_w) {
                for (int py = py0; py < py1 && py < oy + region_h; py++) {
                    canvas.blend_pixel(px, py, r, g, b, 0.35f);
                }
            }
        }
    }
}

#endif // POSTCARD_SC16_H
