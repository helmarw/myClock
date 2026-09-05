#pragma once

#include <cstdint>
#include <ctime>

struct ClockConfig;

struct WeatherStatus {
    bool enabled = false;
    bool valid = false;
    float temperature = 0.0f;
    int humidity = 0;
    float wind_speed = 0.0f;
    int wind_deg = 0;
    int condition_id = 0;
    char wind_dir[4]{};
    char description[80]{};
    char city[64]{};
    char error[128]{};
    std::time_t last_update = 0;
    uint32_t sequence = 0;
};

bool weather_service_start(const ClockConfig& config);
void weather_service_get_status(WeatherStatus& status);
void weather_service_request_update();
