#pragma once

#include <cstdint>

struct ClockConfig {
    char wifi_ssid[33]{};
    char wifi_password[65]{};
    char admin_password[65]{};

    // Time zone: the browser/manual UI stores an IANA name and the firmware
    // stores the corresponding POSIX TZ string used by newlib/localtime().
    char timezone_name[48]{};
    char timezone[80]{};
    bool timezone_auto = true;

    uint8_t brightness = 255;
    bool use_24h = true;
    // Clock color. clock_theme is 0..9 for a matching preset, 255 for a custom color.
    uint8_t clock_theme = 0;
    uint8_t clock_r = 32;
    uint8_t clock_g = 64;
    uint8_t clock_b = 160;

    // OpenWeather current-weather settings (same provider as the original myClock).
    bool weather_enabled = false;
    char openweather_key[65]{};
    char weather_zip[17]{};
    char weather_country[3]{};
    char weather_language[6]{};
    bool weather_celsius = true;
    uint16_t weather_interval_min = 15;

    // Same two local sensors as the original myClock firmware. Pins stay fixed:
    // DS18B20 = GPIO17, LDR = A0/GPIO36 on MH-ET LIVE ESP32MiniKit.
    bool ds18_enabled = true;
    int8_t ds18_temp_offset = 0;
    bool ldr_enabled = true;
    uint16_t ldr_threshold = 1000;
};

void config_set_defaults(ClockConfig& cfg);
bool config_load(ClockConfig& cfg);
bool config_save(const ClockConfig& cfg);
