#pragma once

#include <cstddef>
#include <cstdint>

struct ClockThemeChoice {
    uint8_t id;
    const char* key;
    const char* label;
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

const ClockThemeChoice* clock_theme_choices(std::size_t& count);
const ClockThemeChoice& clock_theme_by_id(uint8_t id);
uint8_t clock_theme_id_for_rgb(uint8_t r, uint8_t g, uint8_t b);
