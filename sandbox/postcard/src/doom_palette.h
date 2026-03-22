/**
 * doom_palette.h - DOOM PLAYPAL color tables for postcard rendering
 *
 * Colors sampled from the actual DOOM PLAYPAL palette.
 * https://doomwiki.org/wiki/PLAYPAL
 * https://lospec.com/palette-list/playpal
 */
#ifndef DOOM_PALETTE_H
#define DOOM_PALETTE_H

#include <cstdint>

// Blood splatter colors (for I/Q scatter) - PLAYPAL reds
// clang-format off
static const uint8_t DOOM_BLOOD[][3] = {
    {0x4f,0x00,0x00}, {0x5b,0x00,0x00}, {0x67,0x00,0x00},
    {0x73,0x00,0x00}, {0x7f,0x00,0x00}, {0x8b,0x00,0x00},
    {0x9b,0x00,0x00}, {0xa7,0x00,0x00}, {0xb3,0x00,0x00},
    {0xbf,0x00,0x00}, {0xcb,0x00,0x00}, {0xd7,0x00,0x00},
    {0xe3,0x00,0x00}, {0xef,0x00,0x00}, {0xff,0x00,0x00},
    {0xff,0x1f,0x1f}, {0xff,0x3f,0x3f}, {0xff,0x5f,0x5f},
};
// clang-format on
static const int DOOM_BLOOD_COUNT = sizeof(DOOM_BLOOD) / sizeof(DOOM_BLOOD[0]);

// Fire/lava gradient for spectrogram (low intensity -> high intensity)
// Dark palette: noise floor stays near-black, signal peaks in deep red
// clang-format off
static const uint8_t DOOM_FIRE[][3] = {
    {0x00,0x00,0x00}, {0x0b,0x07,0x00}, {0x17,0x0f,0x07}, // black
    {0x1f,0x17,0x0b}, {0x2b,0x13,0x07}, {0x37,0x0b,0x00}, // dark brown
    {0x43,0x00,0x00}, {0x47,0x00,0x00}, {0x4f,0x00,0x00}, // dark maroon
    {0x5b,0x00,0x00}, {0x67,0x00,0x00}, {0x73,0x00,0x00}, // maroon
    {0x7f,0x00,0x00}, {0x8b,0x00,0x00}, {0x9b,0x00,0x00}, // blood red
    {0xa7,0x00,0x00}, {0xb3,0x00,0x00}, {0xbf,0x00,0x00}, // bright blood
    {0xcb,0x00,0x00}, {0xd7,0x00,0x00}, {0xe3,0x00,0x00}, // red
    {0xef,0x00,0x00}, {0xff,0x00,0x00}, {0xff,0x1f,0x1f}, // bright red
};
// clang-format on
static const int DOOM_FIRE_COUNT = sizeof(DOOM_FIRE) / sizeof(DOOM_FIRE[0]);

// Look up a fire color by normalized intensity [0..1]
static inline void doom_fire_color(float t, uint8_t& r, uint8_t& g, uint8_t& b) {
    int ci = (int)(t * (DOOM_FIRE_COUNT - 1));
    if (ci < 0) ci = 0;
    if (ci >= DOOM_FIRE_COUNT) ci = DOOM_FIRE_COUNT - 1;
    r = DOOM_FIRE[ci][0];
    g = DOOM_FIRE[ci][1];
    b = DOOM_FIRE[ci][2];
}

// Background colors
static const uint8_t DOOM_BG_R = 27, DOOM_BG_G = 17, DOOM_BG_B = 8;     // deep brown-black
static const uint8_t DOOM_HDR_R = 19, DOOM_HDR_G = 12, DOOM_HDR_B = 4;   // header dark brown
static const uint8_t DOOM_TXT_R = 43, DOOM_TXT_G = 23, DOOM_TXT_B = 8;   // text bar warm brown

// Text colors
static const uint8_t DOOM_GOLD_R = 223, DOOM_GOLD_G = 123, DOOM_GOLD_B = 47;    // #DF7B2F
static const uint8_t DOOM_WARM_R = 200, DOOM_WARM_G = 160, DOOM_WARM_B = 100;   // warm gold
static const uint8_t DOOM_SANDY_R = 190, DOOM_SANDY_G = 150, DOOM_SANDY_B = 90; // sandy gold

// UI accent colors
static const uint8_t DOOM_DIVIDER_R = 120, DOOM_DIVIDER_G = 30, DOOM_DIVIDER_B = 5;   // divider lines
static const uint8_t DOOM_FRAME_R = 140, DOOM_FRAME_G = 25, DOOM_FRAME_B = 8;         // frame border
static const uint8_t DOOM_VIGNETTE_R = 80, DOOM_VIGNETTE_G = 10, DOOM_VIGNETTE_B = 5; // vignette
static const uint8_t DOOM_YELLOW_R = 255, DOOM_YELLOW_G = 200, DOOM_YELLOW_B = 50;    // "IN SPACE!" text
static const uint8_t DOOM_BORDER_R = 100, DOOM_BORDER_G = 20, DOOM_BORDER_B = 5;      // outer border

#endif // DOOM_PALETTE_H
