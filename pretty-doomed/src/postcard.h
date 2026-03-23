/**
 * postcard.h - Composite postcard image generator for PRETTY DOOMed
 *
 * Generates a DOOM-themed postcard from run artifacts: DOOM frame,
 * I/Q scatter (blood splatter), DFT spectrogram, logos, and metadata.
 */
#ifndef POSTCARD_H
#define POSTCARD_H

#include <string>

struct PostcardArgs {
    std::string frame_path;      // DOOM frame JPG or GIF
    std::string sc16_path;       // capture.sc16 (empty = skip scatter/spectrogram)
    std::string transcription;   // voice transcription text content
    std::string demo_name;       // e.g., "e1m7-607"
    std::string timestamp;       // e.g. Georges' birthday, "2026-04-09 12:00:00 UTC"
    std::string logo_esa;        // assets/logo-esa.png
    std::string logo_doom;       // assets/logo-doom.png
    std::string logo_pretty;     // assets/logo-opssat-pretty.png
    std::string output_path;     // where to write postcard.png
    int scale = 1;               // output resolution:1 for 1x, 2 for 2x, 3 for 3x, ..., N for Nx
};

// Generate a composite postcard image. Returns true on success.
bool generate_postcard(const PostcardArgs& args);

#endif // POSTCARD_H
