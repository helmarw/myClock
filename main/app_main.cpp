#include <cmath>
#include <cstdio>
#include <ctime>
#include <cstdint>
#include <string>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "app_config.hpp"
#include "clock_theme.hpp"
#include "digit.hpp"
#include "matrix_display.hpp"
#include "sensor_service.hpp"
#include "tiny_font.hpp"
#include "weather_service.hpp"
#include "web_server.hpp"
#include "wifi_manager.hpp"
#include "wifi_time.hpp"

static const char* TAG = "myClock";

namespace {
constexpr int TOP_Y = 0;
constexpr int BOTTOM_Y = 26;
const uint16_t BLACK = MatrixDisplay::color565(0, 0, 0);
const uint16_t RED = MatrixDisplay::color565(255, 0, 0);
const uint16_t GREEN = MatrixDisplay::color565(0, 255, 0);
const uint16_t BLUE = MatrixDisplay::color565(0, 0, 255);
const uint16_t LTBLUE = MatrixDisplay::color565(32, 64, 160);
const uint16_t WHITE = MatrixDisplay::color565(255, 255, 255);
const uint16_t YELLOW = MatrixDisplay::color565(255, 255, 0);
const uint16_t ORANGE = MatrixDisplay::color565(255, 165, 0);
const uint16_t CYAN = MatrixDisplay::color565(0, 255, 255);
const uint16_t GRAY = MatrixDisplay::color565(102, 102, 102);

const char* WDAY[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
const char* MONTH[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

uint16_t condition_color(int id) {
    switch (id / 100) {
        case 2: return ORANGE; // thunderstorms
        case 3: return BLUE;   // drizzle
        case 5: return BLUE;   // rain
        case 6: return WHITE;  // snow
        case 7: return YELLOW; // atmosphere
        case 8: return GRAY;   // clear/clouds
        default: return LTBLUE;
    }
}

uint16_t temperature_color(float temperature, bool celsius) {
    // Preserve the original clock's Fahrenheit color thresholds even when
    // the displayed value is Celsius.
    const float f = celsius ? temperature * 1.8f + 32.0f : temperature;
    if (f <= 32.0f) return CYAN;
    if (f <= 50.0f) return LTBLUE;
    if (f <= 60.0f) return BLUE;
    if (f <= 78.0f) return GREEN;
    if (f <= 86.0f) return YELLOW;
    if (f <= 95.0f) return ORANGE;
    return RED;
}

void draw_initial_time(Digit& d0, Digit& d1, Digit& d2, Digit& d3, Digit& d4, Digit& d5,
                       uint16_t color, int hh, int mm, int ss) {
    d0.Draw(ss % 10, color);
    d1.Draw(ss / 10, color);
    d2.Draw(mm % 10, color);
    d3.Draw(mm / 10, color);
    d4.Draw(hh % 10, color);
    d5.Draw(hh / 10, color);
    d1.DrawColon(color);
    d3.DrawColon(color);
}

int display_hour(int h, bool use_24h) {
    if (use_24h) return h;
    if (h > 12) h -= 12;
    if (h == 0) h = 12;
    return h;
}

void draw_date(MatrixDisplay& display, const struct tm& ti, uint16_t color) {
    char text[24];
    std::snprintf(text, sizeof(text), "%s %d %s %04d",
                  WDAY[ti.tm_wday], ti.tm_mday, MONTH[ti.tm_mon], ti.tm_year + 1900);
    display.fillRect(0, 25, MatrixDisplay::WIDTH, 7, BLACK);
    tiny_draw_text_centered(display, BOTTOM_Y, text, color);
}

void draw_weather_description(MatrixDisplay& display, const WeatherStatus& weather) {
    std::string text = tiny_ascii_text(weather.description);
    if (text.empty()) text = "WEATHER";
    if (text.size() > 16) text.resize(16); // 16 * 4 - 1 = 63 px
    display.fillRect(0, 25, MatrixDisplay::WIDTH, 7, BLACK);
    tiny_draw_text_centered(display, BOTTOM_Y, text.c_str(), condition_color(weather.condition_id));
}

float local_temperature_for_display(const SensorStatus& sensors, const ClockConfig& config) {
    float t = sensors.ds18_temperature_c;
    if (!config.weather_celsius) t = t * 1.8f + 32.0f;
    return t + static_cast<float>(config.ds18_temp_offset);
}

void draw_weather_top(MatrixDisplay& display, const WeatherStatus& weather,
                      const SensorStatus& sensors, const ClockConfig& config) {
    display.fillRect(0, 0, MatrixDisplay::WIDTH, 6, BLACK);

    const bool local_ok = sensors.ds18_enabled && sensors.ds18_valid;
    const float local_temp = local_ok ? local_temperature_for_display(sensors, config) : 0.0f;

    if (weather.enabled && weather.valid) {
        if (local_ok) {
            // Same layout idea as the original sketch: local DS18B20 temperature
            // first, then slash + outdoor weather data.
            char local[8];
            char rest[24];
            std::snprintf(local, sizeof(local), "%2d", static_cast<int>(std::lround(local_temp)));
            std::snprintf(rest, sizeof(rest), "/%d%c %d%% %d %s",
                          static_cast<int>(std::lround(weather.temperature)),
                          config.weather_celsius ? 'C' : 'F',
                          weather.humidity,
                          static_cast<int>(std::lround(weather.wind_speed)),
                          weather.wind_dir);
            // Center the complete top status line as one unit while keeping
            // the local and outdoor temperatures in their individual colors.
            // The whole top band is cleared above, so every update is a clean redraw.
            const int local_w = tiny_text_width(local);
            const int rest_w = tiny_text_width(rest);
            constexpr int segment_gap = 1;
            const int total_w = local_w + segment_gap + rest_w;
            const int start_x = total_w < MatrixDisplay::WIDTH
                                    ? (MatrixDisplay::WIDTH - total_w) / 2
                                    : 0;
            tiny_draw_text(display, start_x, TOP_Y, local,
                           temperature_color(local_temp, config.weather_celsius));
            tiny_draw_text(display, start_x + local_w + segment_gap, TOP_Y, rest,
                           temperature_color(weather.temperature, config.weather_celsius));
        } else {
            char text[24];
            std::snprintf(text, sizeof(text), "%d%c %d%% %d %s",
                          static_cast<int>(std::lround(weather.temperature)),
                          config.weather_celsius ? 'C' : 'F',
                          weather.humidity,
                          static_cast<int>(std::lround(weather.wind_speed)),
                          weather.wind_dir);
            tiny_draw_text_centered(display, TOP_Y, text,
                                    temperature_color(weather.temperature, config.weather_celsius));
        }
        return;
    }

    // Keep the local sensor useful even if weather is disabled or temporarily unavailable.
    if (local_ok) {
        char text[16];
        std::snprintf(text, sizeof(text), "IN %d%c",
                      static_cast<int>(std::lround(local_temp)),
                      config.weather_celsius ? 'C' : 'F');
        tiny_draw_text_centered(display, TOP_Y, text,
                                temperature_color(local_temp, config.weather_celsius));
        return;
    }

    if (weather.enabled && !weather.valid && weather.error[0]) {
        tiny_draw_text_centered(display, TOP_Y, "WEATHER ERR", RED);
    }
}
}

extern "C" void app_main(void) {
    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(nvs);
    }

    static ClockConfig config{};
    config_load(config);

    static MatrixDisplay display;
    if (!display.begin()) {
        ESP_LOGE(TAG, "Display initialization failed");
        return;
    }
    display.setBrightness(config.brightness);
    display.start_refresh_task();
    sensor_service_start(config);

    const uint16_t myColor = MatrixDisplay::color565(config.clock_r, config.clock_g, config.clock_b);
    if (config.clock_theme <= 9) {
        const ClockThemeChoice& clock_theme = clock_theme_by_id(config.clock_theme);
        ESP_LOGI(TAG, "Clock color: %s #%02X%02X%02X", clock_theme.label, config.clock_r, config.clock_g, config.clock_b);
    } else {
        ESP_LOGI(TAG, "Clock color: custom #%02X%02X%02X", config.clock_r, config.clock_g, config.clock_b);
    }

    // Exact old digit placement. GPIOs/display timings are unchanged from Stage 1/2.
    Digit digit0(&display, 6, 63 - 1 - 9 * 1, 9, myColor);
    Digit digit1(&display, 5, 63 - 1 - 9 * 2, 9, myColor);
    Digit digit2(&display, 4, 63 - 4 - 9 * 3, 9, myColor);
    Digit digit3(&display, 3, 63 - 4 - 9 * 4, 9, myColor);
    Digit digit4(&display, 2, 63 - 7 - 9 * 5, 9, myColor);
    Digit digit5(&display, 1, 63 - 7 - 9 * 6, 9, myColor);

    display.clear();
    draw_initial_time(digit0, digit1, digit2, digit3, digit4, digit5, myColor, 12, 34, 56);

    const bool wifi_ok = wifi_manager_start(config);
    web_server_start(&config, &display); // also available on fallback AP if STA cannot connect

    bool time_ok = wifi_ok && sync_time_from_ntp(config.timezone);
    if (!time_ok) {
        ESP_LOGW(TAG, "No NTP time yet. Web setup remains available; waiting for WiFi/NTP.");
        while (!time_ok) {
            if (wifi_manager_is_connected()) {
                time_ok = sync_time_from_ntp(config.timezone);
                if (!time_ok) vTaskDelay(pdMS_TO_TICKS(10000));
            } else {
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
        }
    }

    std::time_t now = std::time(nullptr);
    struct tm ti{};
    localtime_r(&now, &ti);
    int hh = display_hour(ti.tm_hour, config.use_24h);
    const int ss = ti.tm_sec;
    const int mm = ti.tm_min;

    // Full redraw after NTP sync: keep the display fix that eliminated missing start segments.
    display.clear();
    draw_initial_time(digit0, digit1, digit2, digit3, digit4, digit5, myColor, hh, mm, ss);
    draw_date(display, ti, LTBLUE);
    ESP_LOGI(TAG, "Full display redraw after NTP sync: %02d:%02d:%02d", hh, mm, ss);

    weather_service_start(config);

    int prev_ss = ss;
    int prev_mm = mm;
    int prev_hh = hh;
    int prev_mday = ti.tm_mday;
    int bottom_phase = -1;
    uint32_t last_weather_sequence = UINT32_MAX;
    uint32_t last_sensor_sequence = UINT32_MAX;
    bool ota_screen_shown = false;

    while (true) {
        // The OTA HTTP handler blanks the physical HUB75 output immediately
        // (OE HIGH) before flash writes start. Here we only pause all normal
        // framebuffer updates until OTA either reboots or fails.
        if (web_server_ota_in_progress()) {
            ota_screen_shown = true;
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        // An OTA error can return without rebooting. Rebuild the complete clock
        // once so Digit's internal values and the framebuffer are synchronized.
        if (ota_screen_shown) {
            now = std::time(nullptr);
            localtime_r(&now, &ti);
            const int redraw_hh = display_hour(ti.tm_hour, config.use_24h);
            const int redraw_mm = ti.tm_min;
            const int redraw_ss = ti.tm_sec;

            display.clear();
            draw_initial_time(digit0, digit1, digit2, digit3, digit4, digit5,
                              myColor, redraw_hh, redraw_mm, redraw_ss);

            WeatherStatus weather{};
            weather_service_get_status(weather);
            SensorStatus sensors{};
            sensor_service_get_status(sensors);
            draw_weather_top(display, weather, sensors, config);
            const int redraw_phase = (redraw_ss / 10) & 1;
            if (redraw_phase == 1 && weather.valid) draw_weather_description(display, weather);
            else draw_date(display, ti, weather.valid ? condition_color(weather.condition_id) : LTBLUE);

            prev_ss = redraw_ss;
            prev_mm = redraw_mm;
            prev_hh = redraw_hh;
            prev_mday = ti.tm_mday;
            bottom_phase = redraw_phase;
            last_weather_sequence = weather.sequence;
            last_sensor_sequence = sensors.sequence;
            display.setBrightness(sensors.effective_brightness);
            display.setOutputEnabled(true);
            ota_screen_shown = false;
            ESP_LOGW(TAG, "OTA ended without reboot; full display restored");
        }

        now = std::time(nullptr);
        localtime_r(&now, &ti);
        const int current_hh = display_hour(ti.tm_hour, config.use_24h);
        const int current_mm = ti.tm_min;
        const int current_ss = ti.tm_sec;

        if (current_ss != prev_ss) {
            if (digit0.Value() != current_ss % 10) digit0.Morph(current_ss % 10);
            if (digit1.Value() != current_ss / 10) digit1.Morph(current_ss / 10);
            prev_ss = current_ss;

            WeatherStatus weather{};
            weather_service_get_status(weather);
            SensorStatus sensors{};
            sensor_service_get_status(sensors);
            const bool weather_changed = weather.sequence != last_weather_sequence;
            const bool sensor_changed = sensors.sequence != last_sensor_sequence;
            if (sensor_changed) {
                display.setBrightness(sensors.effective_brightness);
                last_sensor_sequence = sensors.sequence;
            }
            if (weather_changed || sensor_changed) {
                draw_weather_top(display, weather, sensors, config);
                last_weather_sequence = weather.sequence;
                if (weather_changed) {
                    ESP_LOGI(TAG, "Weather display status: valid=%d seq=%u", weather.valid, weather.sequence);
                }
            }

            // Same rhythm as the original: date for 0-9/20-29/40-49,
            // weather description for 10-19/30-39/50-59.
            const int phase = (current_ss / 10) & 1;
            if (phase != bottom_phase || ti.tm_mday != prev_mday || weather_changed) {
                if (phase == 1 && weather.valid) draw_weather_description(display, weather);
                else draw_date(display, ti, weather.valid ? condition_color(weather.condition_id) : LTBLUE);
                bottom_phase = phase;
                prev_mday = ti.tm_mday;
            }
        }
        if (current_mm != prev_mm) {
            if (digit2.Value() != current_mm % 10) digit2.Morph(current_mm % 10);
            if (digit3.Value() != current_mm / 10) digit3.Morph(current_mm / 10);
            prev_mm = current_mm;
        }
        if (current_hh != prev_hh) {
            if (digit4.Value() != current_hh % 10) digit4.Morph(current_hh % 10);
            if (digit5.Value() != current_hh / 10) digit5.Morph(current_hh / 10);
            prev_hh = current_hh;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
