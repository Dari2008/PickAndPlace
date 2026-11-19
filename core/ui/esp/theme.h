/**
 * theme.h - colours and fonts of the display UI (same as mockups/mockups.js)
 */
#pragma once

#include <stdint.h>

namespace th
{
    constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b)
    {
        return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    }
    // `c` at `a` (0..255) over `base`
    constexpr uint16_t mix(uint32_t c, uint32_t base, uint32_t a)
    {
        return rgb((uint8_t)((((c >> 16) & 0xFF) * a + ((base >> 16) & 0xFF) * (255 - a)) / 255),
                   (uint8_t)((((c >> 8) & 0xFF) * a + ((base >> 8) & 0xFF) * (255 - a)) / 255),
                   (uint8_t)(((c & 0xFF) * a + (base & 0xFF) * (255 - a)) / 255));
    }

    // RGB565 `c` at `a` (0..255) over RGB565 `base`
    inline uint16_t tint565(uint16_t c, uint16_t base, uint32_t a)
    {
        const uint32_t r = (((c >> 11) & 31) * a + ((base >> 11) & 31) * (255 - a)) / 255;
        const uint32_t g = (((c >> 5) & 63) * a + ((base >> 5) & 63) * (255 - a)) / 255;
        const uint32_t b = ((c & 31) * a + (base & 31) * (255 - a)) / 255;
        return (uint16_t)((r << 11) | (g << 5) | b);
    }

    // 24-bit sources
    constexpr uint32_t HEX_BG = 0x121417, HEX_PANEL = 0x1C1F24;
    constexpr uint32_t HEX_ACCENT = 0x2DD4BF, HEX_PASTE = 0xF5A524, HEX_OK = 0x4ADE80;
    constexpr uint32_t HEX_WARN = 0xFACC15, HEX_BAD = 0xF2555A, HEX_VIOLET = 0xA78BFA;

    constexpr uint16_t BG = rgb(0x12, 0x14, 0x17);
    constexpr uint16_t PANEL = rgb(0x1C, 0x1F, 0x24);
    constexpr uint16_t PANEL2 = rgb(0x26, 0x2A, 0x31);
    constexpr uint16_t LINE = rgb(0x30, 0x35, 0x3D);
    constexpr uint16_t TEXT = rgb(0xE8, 0xEA, 0xED);
    constexpr uint16_t DIM = rgb(0x8B, 0x92, 0x9C);
    constexpr uint16_t ACCENT = rgb(0x2D, 0xD4, 0xBF);
    constexpr uint16_t ON_ACCENT = rgb(0x06, 0x22, 0x1D); // text on accent buttons
    constexpr uint16_t PASTE = rgb(0xF5, 0xA5, 0x24);
    constexpr uint16_t OK = rgb(0x4A, 0xDE, 0x80);
    constexpr uint16_t WARN = rgb(0xFA, 0xCC, 0x15);
    constexpr uint16_t BAD = rgb(0xF2, 0x55, 0x5A);
    constexpr uint16_t VIOLET = rgb(0xA7, 0x8B, 0xFA);
    constexpr uint16_t WHITE = rgb(0xFF, 0xFF, 0xFF);

    // Tinted backgrounds for pills and icon squares (16 % over the panel)
    constexpr uint16_t ACCENT_BG = mix(HEX_ACCENT, HEX_PANEL, 41);
    constexpr uint16_t PASTE_BG = mix(HEX_PASTE, HEX_PANEL, 41);
    constexpr uint16_t OK_BG = mix(HEX_OK, HEX_PANEL, 38);
    constexpr uint16_t WARN_BG = mix(HEX_WARN, HEX_PANEL, 38);
    constexpr uint16_t BAD_BG = mix(HEX_BAD, HEX_PANEL, 41);
    constexpr uint16_t VIOLET_BG = mix(HEX_VIOLET, HEX_PANEL, 41);
}

// Text styles (mapped to LovyanGFX fonts in gfx.cpp)
enum class Font : uint8_t
{
    Small,    // ~10 px: hints, sub lines
    Body,     // ~12 px
    BodyBold, // ~12 px bold: buttons, pills
    Row,      // ~13 px: list rows
    Title,    // ~13 px bold: title bar, tile labels
    Big,      // ~16 px bold
    Huge,     // ~24 px bold
    Giant     // ~32 px bold: number editor
};

enum class Align : uint8_t
{
    Left,
    Center,
    Right
};
