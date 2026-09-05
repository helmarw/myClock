#pragma once

#include <cstdint>

struct ClockConfig;

struct SensorStatus {
    // DS18B20 on the same GPIO used by the original Arduino firmware.
    bool ds18_enabled = false;
    bool ds18_found = false;
    bool ds18_valid = false;
    float ds18_temperature_c = 0.0f;
    char ds18_error[80]{};

    // LDR on the board's A0 pin. On MH-ET LIVE ESP32MiniKit A0 is GPIO36.
    bool ldr_enabled = false;
    bool ldr_valid = false;
    int ldr_raw = 0;
    int ldr_filtered = 0;
    uint8_t effective_brightness = 255;
    char ldr_error[80]{};

    uint32_t sequence = 0;
};

// Fixed hardware mapping retained from the original myClock project.
static constexpr int MYCLOCK_DS18B20_GPIO = 17;
static constexpr int MYCLOCK_LDR_GPIO = 36; // Arduino A0 on MH-ET LIVE ESP32MiniKit

bool sensor_service_start(const ClockConfig& config);
void sensor_service_get_status(SensorStatus& out);
