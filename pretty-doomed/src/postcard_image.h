/**
 * postcard_image.h - RGBA image buffer with pixel operations, text rendering,
 *                    loading (PNG/JPG/BMP/GIF), and scaling.
 *
 * Requires stb_image.h and stb_image_write.h to be included
 * (with IMPLEMENTATION defines) before this header.
 */
#ifndef POSTCARD_IMAGE_H
#define POSTCARD_IMAGE_H

#include "bitmap_font.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <ctime>
#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>

// Simple RGBA pixel buffer
struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> pixels; // RGBA

    Image() = default;
    Image(int w, int h) : w(w), h(h), pixels(w * h * 4, 0) {}

    void fill(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
        for (int i = 0; i < w * h; i++) {
            pixels[i * 4 + 0] = r;
            pixels[i * 4 + 1] = g;
            pixels[i * 4 + 2] = b;
            pixels[i * 4 + 3] = a;
        }
    }

    void set_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
        if (x < 0 || x >= w || y < 0 || y >= h) return;
        int idx = (y * w + x) * 4;
        pixels[idx + 0] = r;
        pixels[idx + 1] = g;
        pixels[idx + 2] = b;
        pixels[idx + 3] = a;
    }

    void blend_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b, float factor) {
        if (x < 0 || x >= w || y < 0 || y >= h) return;
        int idx = (y * w + x) * 4;
        pixels[idx + 0] = (uint8_t)(pixels[idx + 0] * (1 - factor) + r * factor);
        pixels[idx + 1] = (uint8_t)(pixels[idx + 1] * (1 - factor) + g * factor);
        pixels[idx + 2] = (uint8_t)(pixels[idx + 2] * (1 - factor) + b * factor);
        pixels[idx + 3] = 255;
    }

    void blit(const Image& src, int dx, int dy) {
        for (int sy = 0; sy < src.h; sy++) {
            for (int sx = 0; sx < src.w; sx++) {
                int tx = dx + sx, ty = dy + sy;
                if (tx < 0 || tx >= w || ty < 0 || ty >= h) continue;
                int si = (sy * src.w + sx) * 4;
                int di = (ty * w + tx) * 4;
                uint8_t sa = src.pixels[si + 3];
                if (sa == 255) {
                    pixels[di + 0] = src.pixels[si + 0];
                    pixels[di + 1] = src.pixels[si + 1];
                    pixels[di + 2] = src.pixels[si + 2];
                    pixels[di + 3] = 255;
                } else if (sa > 0) {
                    float a = sa / 255.0f;
                    pixels[di + 0] = (uint8_t)(src.pixels[si + 0] * a + pixels[di + 0] * (1 - a));
                    pixels[di + 1] = (uint8_t)(src.pixels[si + 1] * a + pixels[di + 1] * (1 - a));
                    pixels[di + 2] = (uint8_t)(src.pixels[si + 2] * a + pixels[di + 2] * (1 - a));
                    pixels[di + 3] = 255;
                }
            }
        }
    }

    void draw_text(int x, int y, const char* text, uint8_t r, uint8_t g, uint8_t b,
                   int scale = 2, int line_spacing = -1) {
        int cx = x;
        int lh = (line_spacing > 0) ? line_spacing : (GLYPH_H + 1) * scale;
        for (const char* p = text; *p; p++) {
            if (*p == '\n') { cx = x; y += lh; continue; }
            const Glyph& gl = get_glyph(*p);
            for (int gy = 0; gy < GLYPH_H; gy++) {
                for (int gx = 0; gx < GLYPH_W; gx++) {
                    if (gl.rows[gy] & (0x10 >> gx)) {
                        for (int sy = 0; sy < scale; sy++)
                            for (int sx = 0; sx < scale; sx++)
                                set_pixel(cx + gx * scale + sx, y + gy * scale + sy, r, g, b);
                    }
                }
            }
            cx += (GLYPH_W + 1) * scale;
        }
    }

    void draw_text_gradient(int x, int y, const char* text,
                            uint8_t r1, uint8_t g1, uint8_t b1,
                            uint8_t r2, uint8_t g2, uint8_t b2,
                            int scale = 2, int line_spacing = -1) {
        int num_lines = 1;
        for (const char* p = text; *p; p++) if (*p == '\n') num_lines++;

        int cx = x, cur_y = y, line = 0;
        int lh = (line_spacing > 0) ? line_spacing : (GLYPH_H + 1) * scale;
        for (const char* p = text; *p; p++) {
            if (*p == '\n') { cx = x; cur_y += lh; line++; continue; }
            float t = (num_lines > 1) ? (float)line / (num_lines - 1) : 0.0f;
            uint8_t cr = (uint8_t)(r1 + (r2 - (int)r1) * t);
            uint8_t cg = (uint8_t)(g1 + (g2 - (int)g1) * t);
            uint8_t cb = (uint8_t)(b1 + (b2 - (int)b1) * t);
            const Glyph& gl = get_glyph(*p);
            for (int gy = 0; gy < GLYPH_H; gy++) {
                for (int gx = 0; gx < GLYPH_W; gx++) {
                    if (gl.rows[gy] & (0x10 >> gx)) {
                        for (int sy = 0; sy < scale; sy++)
                            for (int sx = 0; sx < scale; sx++)
                                set_pixel(cx + gx * scale + sx, cur_y + gy * scale + sy, cr, cg, cb);
                    }
                }
            }
            cx += (GLYPH_W + 1) * scale;
        }
    }

    void draw_text_italic(int x, int y, const char* text, uint8_t r, uint8_t g, uint8_t b,
                          int scale = 2) {
        int cx = x;
        for (const char* p = text; *p; p++) {
            const Glyph& gl = get_glyph(*p);
            for (int gy = 0; gy < GLYPH_H; gy++) {
                int skew = (GLYPH_H - 1 - gy) * scale / 3;
                for (int gx = 0; gx < GLYPH_W; gx++) {
                    if (gl.rows[gy] & (0x10 >> gx)) {
                        for (int sy = 0; sy < scale; sy++)
                            for (int sx = 0; sx < scale; sx++)
                                set_pixel(cx + gx * scale + sx + skew, y + gy * scale + sy, r, g, b);
                    }
                }
            }
            cx += (GLYPH_W + 1) * scale;
        }
    }

    void fill_rect(int x, int y, int rw, int rh, uint8_t r, uint8_t g, uint8_t b) {
        for (int ry = y; ry < y + rh; ry++)
            for (int rx = x; rx < x + rw; rx++)
                set_pixel(rx, ry, r, g, b);
    }
};

// Check if path ends with .gif (case insensitive)
static bool is_gif(const char* path) {
    size_t len = strlen(path);
    if (len < 4) return false;
    const char* ext = path + len - 4;
    return (ext[0] == '.' &&
            (ext[1] == 'g' || ext[1] == 'G') &&
            (ext[2] == 'i' || ext[2] == 'I') &&
            (ext[3] == 'f' || ext[3] == 'F'));
}

// Load image file (PNG, JPG, BMP, GIF)
// For animated GIFs, picks a random frame
static Image load_image(const char* path) {
    Image img;

    if (is_gif(path)) {
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f) {
            fprintf(stderr, "Warning: cannot open %s\n", path);
            return img;
        }
        size_t file_size = f.tellg();
        f.seekg(0);
        std::vector<uint8_t> buf(file_size);
        f.read(reinterpret_cast<char*>(buf.data()), file_size);

        int *delays = nullptr;
        int frames = 0, channels;
        uint8_t* data = stbi_load_gif_from_memory(buf.data(), (int)file_size,
            &delays, &img.w, &img.h, &frames, &channels, 4);
        if (!data || frames <= 0) {
            fprintf(stderr, "Warning: cannot load GIF %s: %s\n", path, stbi_failure_reason());
            if (data) stbi_image_free(data);
            if (delays) stbi_image_free(delays);
            return img;
        }

        int pick = rand() % frames;
        int frame_bytes = img.w * img.h * 4;
        uint8_t* frame_data = data + pick * frame_bytes;
        img.pixels.assign(frame_data, frame_data + frame_bytes);
        printf("  GIF: %d frames, picked frame %d\n", frames, pick);

        stbi_image_free(data);
        stbi_image_free(delays);
        return img;
    }

    int channels;
    uint8_t* data = stbi_load(path, &img.w, &img.h, &channels, 4);
    if (!data) {
        fprintf(stderr, "Warning: cannot load %s: %s\n", path, stbi_failure_reason());
        return img;
    }
    img.pixels.assign(data, data + img.w * img.h * 4);
    stbi_image_free(data);
    return img;
}

// Scale image to fit within max_w x max_h, preserving aspect ratio.
// Uses area-average for downscaling, bilinear for upscaling.
static Image scale_to_fit(const Image& src, int max_w, int max_h) {
    if (src.w == 0 || src.h == 0) return src;
    float scale = std::min((float)max_w / src.w, (float)max_h / src.h);
    if (std::abs(scale - 1.0f) < 0.001f) return src;
    int nw = std::max(1, (int)(src.w * scale));
    int nh = std::max(1, (int)(src.h * scale));

    Image dst(nw, nh);

    if (scale < 1.0f) {
        for (int y = 0; y < nh; y++) {
            for (int x = 0; x < nw; x++) {
                float sx0 = x / scale, sy0 = y / scale;
                float sx1 = (x + 1) / scale, sy1 = (y + 1) / scale;
                int ix0 = (int)sx0, iy0 = (int)sy0;
                int ix1 = std::min((int)sx1, src.w - 1);
                int iy1 = std::min((int)sy1, src.h - 1);

                float sum_r = 0, sum_g = 0, sum_b = 0, sum_a = 0;
                int count = 0;
                for (int sy = iy0; sy <= iy1; sy++) {
                    for (int sx = ix0; sx <= ix1; sx++) {
                        int si = (sy * src.w + sx) * 4;
                        sum_r += src.pixels[si + 0];
                        sum_g += src.pixels[si + 1];
                        sum_b += src.pixels[si + 2];
                        sum_a += src.pixels[si + 3];
                        count++;
                    }
                }
                int di = (y * nw + x) * 4;
                dst.pixels[di + 0] = (uint8_t)(sum_r / count);
                dst.pixels[di + 1] = (uint8_t)(sum_g / count);
                dst.pixels[di + 2] = (uint8_t)(sum_b / count);
                dst.pixels[di + 3] = (uint8_t)(sum_a / count);
            }
        }
    } else {
        for (int y = 0; y < nh; y++) {
            for (int x = 0; x < nw; x++) {
                float sx = (x + 0.5f) / scale - 0.5f;
                float sy = (y + 0.5f) / scale - 0.5f;
                int x0 = std::max(0, (int)sx);
                int y0 = std::max(0, (int)sy);
                int x1 = std::min(x0 + 1, src.w - 1);
                int y1 = std::min(y0 + 1, src.h - 1);
                float fx = sx - x0, fy = sy - y0;
                fx = std::max(0.0f, std::min(1.0f, fx));
                fy = std::max(0.0f, std::min(1.0f, fy));

                int di = (y * nw + x) * 4;
                for (int c = 0; c < 4; c++) {
                    float v = src.pixels[(y0 * src.w + x0) * 4 + c] * (1-fx) * (1-fy)
                            + src.pixels[(y0 * src.w + x1) * 4 + c] * fx * (1-fy)
                            + src.pixels[(y1 * src.w + x0) * 4 + c] * (1-fx) * fy
                            + src.pixels[(y1 * src.w + x1) * 4 + c] * fx * fy;
                    dst.pixels[di + c] = (uint8_t)std::min(255.0f, v);
                }
            }
        }
    }
    return dst;
}

#endif // POSTCARD_IMAGE_H
