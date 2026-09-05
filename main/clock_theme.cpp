#include "clock_theme.hpp"

namespace {
constexpr ClockThemeChoice THEMES[] = {
    {0, "original", "Original (Hellblau)", 32, 64, 160},
    {1, "blue",     "Blau",                0, 0, 255},
    {2, "cyan",     "Türkis / Cyan",       0, 255, 255},
    {3, "green",    "Grün",                0, 255, 0},
    {4, "yellow",   "Gelb",                 255, 255, 0},
    {5, "orange",   "Orange",               255, 165, 0},
    {6, "red",      "Rot",                  255, 0, 0},
    {7, "white",    "Weiß",                255, 255, 255},
    {8, "violet",   "Violett",              160, 32, 240},
    {9, "pink",     "Pink",                 255, 64, 160},
};
}

const ClockThemeChoice* clock_theme_choices(std::size_t& count) {
    count = sizeof(THEMES) / sizeof(THEMES[0]);
    return THEMES;
}

const ClockThemeChoice& clock_theme_by_id(uint8_t id) {
    for (const auto& theme : THEMES) {
        if (theme.id == id) return theme;
    }
    return THEMES[0];
}

uint8_t clock_theme_id_for_rgb(uint8_t r, uint8_t g, uint8_t b) {
    for (const auto& theme : THEMES) {
        if (theme.r == r && theme.g == g && theme.b == b) return theme.id;
    }
    return 255;
}
