/**
 * postcard.cpp - Composite postcard image generator for PRETTY DOOMed
 *
 * Composites DOOM frame, I/Q constellation scatter (from raw sc16),
 * logos, and run metadata into a single downlink image.
 *
 * Uses stb single-header libraries (public domain, no dependencies).
 */

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <unistd.h>
#include "image.h"
#include "render_sc16.h"

struct Args {
    const char* frame = nullptr;
    const char* sc16 = nullptr;
    const char* transcription = nullptr;
    const char* demo = nullptr;
    const char* logo_esa = nullptr;
    const char* logo_doom = nullptr;
    const char* logo_pretty = nullptr;
    const char* output = "postcard.png";
    int scale = 1;
};

bool parse_args(int argc, char** argv, Args& args) {
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--frame" && i + 1 < argc) args.frame = argv[++i];
        else if (a == "--sc16" && i + 1 < argc) args.sc16 = argv[++i];
        else if (a == "--transcription" && i + 1 < argc) args.transcription = argv[++i];
        else if (a == "--demo" && i + 1 < argc) args.demo = argv[++i];
        else if (a == "--logo-esa" && i + 1 < argc) args.logo_esa = argv[++i];
        else if (a == "--logo-doom" && i + 1 < argc) args.logo_doom = argv[++i];
        else if (a == "--logo-pretty" && i + 1 < argc) args.logo_pretty = argv[++i];
        else if (a == "--output" && i + 1 < argc) args.output = argv[++i];
        else if (a == "--scale" && i + 1 < argc) args.scale = std::max(1, atoi(argv[++i]));
        else if ((a == "--scores" || a == "--stats") && i + 1 < argc) ++i; // ignored, skip value
        else {
            fprintf(stderr, "Unknown arg: %s\n", argv[i]);
            return false;
        }
    }
    return true;
}

int main(int argc, char** argv) {
    srand((unsigned)time(nullptr) ^ (unsigned)getpid());

    Args args;
    if (!parse_args(argc, argv, args)) return 1;

    // Load images
    Image frame = args.frame ? load_image(args.frame) : Image();
    Image logo_esa = args.logo_esa ? load_image(args.logo_esa) : Image();
    Image logo_doom = args.logo_doom ? load_image(args.logo_doom) : Image();
    Image logo_pretty = args.logo_pretty ? load_image(args.logo_pretty) : Image();

    // Read text
    std::string transcription = args.transcription ? read_file(args.transcription) : "";
    std::string demo_name = args.demo ? args.demo : "";

    // Layout (all dimensions scaled by --scale factor)
    const int S = args.scale;
    const int S2 = 2 * S;
    const int PAD = 20 * S;
    const int LOGO_H = 140 * S;
    const int FRAME_MAX_W = 640 * S;
    const int FRAME_MAX_H = 420 * S;
    const int SCATTER_SIZE = 340 * S;
    const int TEXT_H = 280 * S;

    // Scale images
    Image frame_scaled = scale_to_fit(frame, FRAME_MAX_W, FRAME_MAX_H);
    Image esa_scaled = scale_to_fit(logo_esa, 250 * S, LOGO_H);
    Image doom_scaled = scale_to_fit(logo_doom, 220 * S, LOGO_H);
    Image pretty_scaled = scale_to_fit(logo_pretty, LOGO_H, LOGO_H);

    // Canvas dimensions
    int header_h = PAD + LOGO_H + PAD;
    int body_h = std::max(frame_scaled.h, SCATTER_SIZE);
    int canvas_w = PAD + frame_scaled.w + 10 * S + SCATTER_SIZE + PAD;
    if (canvas_w < 1260 * S) canvas_w = 1260 * S;

    // Extract timestamp from log file
    std::string timestamp;
    if (args.transcription) {
        std::string log_dir = args.transcription;
        auto slash = log_dir.find_last_of('/');
        std::string log_path = (slash != std::string::npos)
            ? log_dir.substr(0, slash + 1) + "pretty-doomed.log"
            : "pretty-doomed.log";
        std::string log_content = read_file(log_path.c_str());
        // Find timestamp from "Command detected!" line
        auto cmd_pos = log_content.find("Command detected!");
        if (cmd_pos != std::string::npos) {
            // Search backward for the '[' bracket on that line
            auto bracket = log_content.rfind('[', cmd_pos);
            if (bracket != std::string::npos) {
                auto end = log_content.find(']', bracket);
                if (end != std::string::npos) {
                    timestamp = log_content.substr(bracket + 1, end - bracket - 1);
                    auto dot = timestamp.find('.');
                    if (dot != std::string::npos) timestamp = timestamp.substr(0, dot);
                    timestamp = "COMMAND DETECTED AT " + timestamp + " UTC";
                }
            }
        }
    }

    // --- Compute text area height before creating canvas ---
    int tx = PAD + 8 * S;
    int text_area_w = canvas_w - PAD * 2 - 16 * S;
    int small_lh = (GLYPH_H + 2) * S2;
    int line_gap = 6 * S;

    // Count bottom lines: PLAY, timestamp|URL, attribution
    int bottom_lines = 1; // attribution
    if (!demo_name.empty()) bottom_lines++;
    bottom_lines++; // timestamp | URL line
    int bottom_h = bottom_lines * small_lh + (bottom_lines - 1) * line_gap;

    // Word-wrap helper
    auto wrap_text = [&](const std::string& text, int try_scale, int max_lines) {
        int cw = (GLYPH_W + 1) * try_scale;
        int max_cpl = text_area_w / cw;
        std::string w;
        int c = 0, nl = 1;
        std::istringstream ws(text);
        std::string wd;
        while (ws >> wd) {
            if (c > 0 && c + 1 + (int)wd.size() > max_cpl) {
                if (max_lines > 0 && nl >= max_lines) break;
                w += '\n'; c = 0; nl++;
            }
            if (c > 0) { w += ' '; c++; }
            w += wd; c += (int)wd.size();
        }
        return std::make_pair(w, nl);
    };

    // Auto-scale transcript: largest font where all text fits
    int transcript_avail_h = TEXT_H - bottom_h - 2 * line_gap;
    int best_scale = S;
    std::string best_wrapped;
    for (int try_scale = 7 * S; try_scale >= S; try_scale -= S) {
        int max_cpl = text_area_w / ((GLYPH_W + 1) * try_scale);
        if (max_cpl < 10) continue;

        auto [w, nl] = wrap_text(transcription, try_scale, 0);
        // Use glyph height + minimal gap for fit check (actual spacing set later)
        int fit_h = (GLYPH_H + 1) * try_scale;
        if (nl * fit_h <= transcript_avail_h) {
            best_scale = try_scale;
            best_wrapped = w;
            break;
        }
    }
    if (best_wrapped.empty() && !transcription.empty()) {
        int lh = (GLYPH_H + 3) * S;
        int max_lines = std::max(1, transcript_avail_h / lh);
        auto [w, nl] = wrap_text(transcription, S, max_lines);
        best_scale = S;
        best_wrapped = w;
    }

    // Compute actual transcript height
    int num_lines = 1;
    for (char ch : best_wrapped) if (ch == '\n') num_lines++;
    int best_line_h = (GLYPH_H + 3) * best_scale;
    int transcript_h = num_lines * best_line_h;

    // Total content height and dynamic TEXT_H
    int content_h = transcript_h + 2 * line_gap + bottom_h + 2 * line_gap;
    int actual_text_h = std::max(content_h, TEXT_H);

    // Create canvas with final dimensions
    int canvas_h = header_h + body_h + PAD + actual_text_h + PAD;
    Image canvas(canvas_w, canvas_h);
    canvas.fill(DOOM_BG_R, DOOM_BG_G, DOOM_BG_B);

    // --- Header ---
    canvas.fill_rect(0, 0, canvas_w, header_h, DOOM_HDR_R, DOOM_HDR_G, DOOM_HDR_B);

    // Mission patch at absolute horizontal center
    int pretty_x = canvas_w / 2 - pretty_scaled.w / 2;
    int pretty_y = (header_h - pretty_scaled.h) / 2;

    // ESA logo left (resize if it would overlap the patch)
    int esa_max_w = pretty_x - PAD - PAD;
    Image esa_final = (esa_scaled.w > esa_max_w)
        ? scale_to_fit(logo_esa, esa_max_w, LOGO_H)
        : esa_scaled;
    int logo_y = (header_h - esa_final.h) / 2;
    canvas.blit(esa_final, PAD, logo_y);

    canvas.blit(pretty_scaled, pretty_x, pretty_y);

    // DOOM logo right
    int doom_y = (header_h - doom_scaled.h) / 2;
    canvas.blit(doom_scaled, canvas_w - PAD - doom_scaled.w, doom_y);

    // "IN SPACE!" italic, tucked under the OO of DOOM
    {
        const char* in_space = "IN SPACE!";
        int isp_char_w = (GLYPH_W + 1) * S2;
        int isp_skew = (GLYPH_H - 1) * S2 / 3;
        int isp_text_w = (int)strlen(in_space) * isp_char_w + isp_skew;
        int doom_center_x = canvas_w - PAD - doom_scaled.w / 2;
        int isp_x = doom_center_x - isp_text_w / 2;
        int isp_y = doom_y + doom_scaled.h - 14 * S;
        canvas.draw_text_italic(isp_x, isp_y, in_space,
            DOOM_YELLOW_R, DOOM_YELLOW_G, DOOM_YELLOW_B, S2);
    }

    canvas.fill_rect(PAD, header_h - S, canvas_w - PAD * 2, S,
        DOOM_DIVIDER_R, DOOM_DIVIDER_G, DOOM_DIVIDER_B);

    // --- Body: I/Q scatter + DOOM frame ---
    int body_y = header_h + PAD / 2;
    int body_w = canvas_w - PAD * 2;

    // Dark red vignette
    int cx = PAD + body_w / 2;
    int cy = body_y + body_h / 2;
    float max_r = body_w * 0.45f;
    for (int vy = body_y; vy < body_y + body_h; vy++) {
        for (int vx = PAD; vx < PAD + body_w; vx++) {
            float dx = vx - cx, dy = vy - cy;
            float dist = std::sqrt(dx * dx + dy * dy);
            if (dist < max_r) {
                float t = 1.0f - dist / max_r;
                t = t * t * 0.12f;
                canvas.blend_pixel(vx, vy,
                    DOOM_VIGNETTE_R, DOOM_VIGNETTE_G, DOOM_VIGNETTE_B, t);
            }
        }
    }

    if (args.sc16) {
        render_iq_scatter(canvas, PAD, body_y, body_w, body_h, args.sc16, 120000, S);
    }

    // DOOM frame centered in body
    int frame_x = PAD + (body_w - frame_scaled.w) / 2;
    int frame_y = body_y + (body_h - frame_scaled.h) / 2;
    canvas.blit(frame_scaled, frame_x, frame_y);

    // Frame border
    for (int i = 0; i < S2; i++) {
        for (int x = frame_x - i - 1; x <= frame_x + frame_scaled.w + i; x++) {
            canvas.set_pixel(x, frame_y - i - 1,
                DOOM_FRAME_R, DOOM_FRAME_G, DOOM_FRAME_B);
            canvas.set_pixel(x, frame_y + frame_scaled.h + i,
                DOOM_FRAME_R, DOOM_FRAME_G, DOOM_FRAME_B);
        }
        for (int y = frame_y - i - 1; y <= frame_y + frame_scaled.h + i; y++) {
            canvas.set_pixel(frame_x - i - 1, y,
                DOOM_FRAME_R, DOOM_FRAME_G, DOOM_FRAME_B);
            canvas.set_pixel(frame_x + frame_scaled.w + i, y,
                DOOM_FRAME_R, DOOM_FRAME_G, DOOM_FRAME_B);
        }
    }

    // --- Text bar with spectrogram background ---
    int divider_y = body_y + body_h + PAD / 2;
    canvas.fill_rect(PAD, divider_y, canvas_w - PAD * 2, S,
        DOOM_DIVIDER_R, DOOM_DIVIDER_G, DOOM_DIVIDER_B);

    int text_y = divider_y + PAD / 2;
    canvas.fill_rect(PAD, text_y, canvas_w - PAD * 2, actual_text_h, DOOM_TXT_R, DOOM_TXT_G, DOOM_TXT_B);
    if (args.sc16) {
        render_spectrogram_bg(canvas, PAD, text_y,
            canvas_w - PAD * 2, actual_text_h, args.sc16);
    }

    // Transcript: top-aligned
    int cursor = text_y + line_gap;
    canvas.draw_text_gradient(tx, cursor, best_wrapped.c_str(),
        240, 195, 90, 170, 75, 15, best_scale, best_line_h);

    // Bottom lines: bottom-aligned, rendered upward from text area bottom
    int text_area_bottom = text_y + actual_text_h - line_gap;
    int row = text_area_bottom - small_lh;

    // Last line: attribution (single line, auto-sized to fit width)
    const char* attrib_text = "THIS POSTCARD WAS SENT FROM LOW EARTH ORBIT BY THE EUROPEAN SPACE AGENCY'S OPS-SAT PRETTY SPACECRAFT";
    int attrib_len = (int)strlen(attrib_text);
    int attrib_scale = text_area_w / ((GLYPH_W + 1) * attrib_len);
    if (attrib_scale < 1) attrib_scale = 1;
    if (attrib_scale > S2) attrib_scale = S2;
    int attrib_lh = (GLYPH_H + 2) * attrib_scale;
    canvas.draw_text(tx, row + (small_lh - attrib_lh), attrib_text,
        DOOM_SANDY_R, DOOM_SANDY_G, DOOM_SANDY_B, attrib_scale);
    row -= small_lh + line_gap;

    // Timestamp | URL
    std::string ts_url = timestamp.empty()
        ? "OPSSAT.ESA.INT"
        : timestamp + "  |  OPSSAT.ESA.INT";
    canvas.draw_text(tx, row, ts_url.c_str(),
        DOOM_WARM_R, DOOM_WARM_G, DOOM_WARM_B, S2);
    row -= small_lh + line_gap;

    // PLAY line
    if (!demo_name.empty()) {
        std::string line = "PLAY: " + demo_name + "  |  VOICE-COMMANDED FROM EARTH TO RUN DOOM IN SPACE";
        canvas.draw_text(tx, row, line.c_str(),
            DOOM_GOLD_R, DOOM_GOLD_G, DOOM_GOLD_B, S2);
    }

    // --- Outer border ---
    for (int i = 0; i < S2; i++) {
        for (int x = 0; x < canvas_w; x++) {
            canvas.set_pixel(x, i, DOOM_BORDER_R, DOOM_BORDER_G, DOOM_BORDER_B);
            canvas.set_pixel(x, canvas_h - 1 - i, DOOM_BORDER_R, DOOM_BORDER_G, DOOM_BORDER_B);
        }
        for (int y = 0; y < canvas_h; y++) {
            canvas.set_pixel(i, y, DOOM_BORDER_R, DOOM_BORDER_G, DOOM_BORDER_B);
            canvas.set_pixel(canvas_w - 1 - i, y, DOOM_BORDER_R, DOOM_BORDER_G, DOOM_BORDER_B);
        }
    }

    // Write output
    bool ok;
    std::string out_path = args.output;
    if (out_path.size() > 4 && out_path.substr(out_path.size() - 4) == ".png") {
        ok = stbi_write_png(args.output, canvas.w, canvas.h, 4, canvas.pixels.data(), canvas.w * 4);
    } else {
        ok = stbi_write_jpg(args.output, canvas.w, canvas.h, 4, canvas.pixels.data(), 92);
    }
    if (!ok) {
        fprintf(stderr, "Error: cannot write %s\n", args.output);
        return 1;
    }

    printf("Postcard: %s (%dx%d)\n", args.output, canvas.w, canvas.h);
    return 0;
}
