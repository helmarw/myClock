#include "weather_service.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "app_config.hpp"
#include "wifi_manager.hpp"

static const char* TAG = "weather";

namespace {
ClockConfig s_cfg{};
WeatherStatus s_status{};
SemaphoreHandle_t s_mutex = nullptr;
TaskHandle_t s_task = nullptr;

void safe_copy(char* dst, size_t dst_size, const char* src) {
    if (!dst || dst_size == 0) return;
    if (!src) src = "";
    std::strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

void set_error(const char* message) {
    if (!s_mutex) return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_status.valid = false;
    safe_copy(s_status.error, sizeof(s_status.error), message);
    ++s_status.sequence;
    xSemaphoreGive(s_mutex);
}

std::string url_encode(const char* text) {
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    if (!text) return out;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; ++p) {
        const unsigned char c = *p;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 0x0F]);
        }
    }
    return out;
}

esp_err_t http_event(esp_http_client_event_t* evt) {
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data && evt->data_len > 0 && evt->user_data) {
        auto* body = static_cast<std::string*>(evt->user_data);
        if (body->size() + static_cast<size_t>(evt->data_len) <= 8192) {
            body->append(static_cast<const char*>(evt->data), static_cast<size_t>(evt->data_len));
        }
    }
    return ESP_OK;
}

const char* direction(int degrees) {
    static const char* dirs[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    int d = degrees % 360;
    if (d < 0) d += 360;
    return dirs[((d + 22) / 45) % 8];
}

bool parse_weather(const std::string& payload, WeatherStatus& out) {
    cJSON* root = cJSON_ParseWithLength(payload.c_str(), payload.size());
    if (!root) return false;

    cJSON* cod = cJSON_GetObjectItemCaseSensitive(root, "cod");
    if (cJSON_IsNumber(cod) && cod->valueint != 200) {
        cJSON* message = cJSON_GetObjectItemCaseSensitive(root, "message");
        if (cJSON_IsString(message) && message->valuestring) safe_copy(out.error, sizeof(out.error), message->valuestring);
        cJSON_Delete(root);
        return false;
    }

    cJSON* main = cJSON_GetObjectItemCaseSensitive(root, "main");
    cJSON* weather = cJSON_GetObjectItemCaseSensitive(root, "weather");
    cJSON* wind = cJSON_GetObjectItemCaseSensitive(root, "wind");
    if (!cJSON_IsObject(main) || !cJSON_IsArray(weather)) {
        cJSON_Delete(root);
        return false;
    }

    cJSON* temp = cJSON_GetObjectItemCaseSensitive(main, "temp");
    cJSON* humidity = cJSON_GetObjectItemCaseSensitive(main, "humidity");
    cJSON* w0 = cJSON_GetArrayItem(weather, 0);
    cJSON* id = w0 ? cJSON_GetObjectItemCaseSensitive(w0, "id") : nullptr;
    cJSON* desc = w0 ? cJSON_GetObjectItemCaseSensitive(w0, "description") : nullptr;
    cJSON* speed = cJSON_IsObject(wind) ? cJSON_GetObjectItemCaseSensitive(wind, "speed") : nullptr;
    cJSON* deg = cJSON_IsObject(wind) ? cJSON_GetObjectItemCaseSensitive(wind, "deg") : nullptr;
    cJSON* name = cJSON_GetObjectItemCaseSensitive(root, "name");

    if (!cJSON_IsNumber(temp) || !cJSON_IsNumber(humidity) || !cJSON_IsNumber(id)) {
        cJSON_Delete(root);
        return false;
    }

    out.temperature = static_cast<float>(temp->valuedouble);
    out.humidity = humidity->valueint;
    out.condition_id = id->valueint;
    out.wind_speed = cJSON_IsNumber(speed) ? static_cast<float>(speed->valuedouble) : 0.0f;
    out.wind_deg = cJSON_IsNumber(deg) ? deg->valueint : 0;
    safe_copy(out.wind_dir, sizeof(out.wind_dir), direction(out.wind_deg));
    safe_copy(out.description, sizeof(out.description), cJSON_IsString(desc) ? desc->valuestring : "");
    safe_copy(out.city, sizeof(out.city), cJSON_IsString(name) ? name->valuestring : "");
    out.error[0] = '\0';
    out.valid = true;
    out.last_update = std::time(nullptr);

    cJSON_Delete(root);
    return true;
}

bool fetch_once() {
    if (!s_cfg.weather_enabled) return false;
    if (!s_cfg.openweather_key[0] || !s_cfg.weather_zip[0] || !s_cfg.weather_country[0]) {
        set_error("Wetter ist aktiviert, aber API-Key/PLZ/Land fehlt");
        return false;
    }
    if (!wifi_manager_is_connected()) {
        set_error("Kein WLAN fuer Wetterabfrage");
        return false;
    }

    std::string url = "https://api.openweathermap.org/data/2.5/weather?zip=";
    url += url_encode(s_cfg.weather_zip);
    url += ",";
    url += url_encode(s_cfg.weather_country);
    url += "&units=";
    url += s_cfg.weather_celsius ? "metric" : "imperial";
    url += "&lang=";
    url += url_encode(s_cfg.weather_language[0] ? s_cfg.weather_language : "en");
    url += "&appid=";
    url += url_encode(s_cfg.openweather_key);

    std::string payload;
    esp_http_client_config_t cfg{};
    cfg.url = url.c_str();
    cfg.event_handler = http_event;
    cfg.user_data = &payload;
    cfg.user_agent = "myClock-ESP-IDF/2.1";
    cfg.timeout_ms = 12000;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        set_error("HTTP-Client konnte nicht erstellt werden");
        return false;
    }

    const esp_err_t err = esp_http_client_perform(client);
    const int status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        char msg[128];
        std::snprintf(msg, sizeof(msg), "OpenWeather HTTPS: %s", esp_err_to_name(err));
        set_error(msg);
        ESP_LOGW(TAG, "%s", msg);
        return false;
    }
    if (status_code != 200) {
        char msg[128];
        std::snprintf(msg, sizeof(msg), "OpenWeather HTTP %d", status_code);
        // If OpenWeather returned a JSON error, parse_weather will copy its message.
        WeatherStatus candidate{};
        candidate.enabled = true;
        if (!payload.empty()) parse_weather(payload, candidate);
        if (candidate.error[0]) std::snprintf(msg, sizeof(msg), "OpenWeather HTTP %d: %.90s", status_code, candidate.error);
        set_error(msg);
        ESP_LOGW(TAG, "%s", msg);
        return false;
    }

    WeatherStatus candidate{};
    candidate.enabled = true;
    if (!parse_weather(payload, candidate)) {
        set_error("OpenWeather JSON konnte nicht gelesen werden");
        ESP_LOGW(TAG, "Could not parse OpenWeather response (%u bytes)", static_cast<unsigned>(payload.size()));
        return false;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    const uint32_t next_sequence = s_status.sequence + 1;
    s_status = candidate;
    s_status.sequence = next_sequence;
    xSemaphoreGive(s_mutex);

    ESP_LOGI(TAG, "%s: %.1f %s, %d%%, wind %.1f %s, id=%d, '%s'",
             candidate.city,
             candidate.temperature, s_cfg.weather_celsius ? "C" : "F",
             candidate.humidity, candidate.wind_speed, candidate.wind_dir,
             candidate.condition_id, candidate.description);
    return true;
}

void weather_task(void*) {
    // Give NTP/cert validation and the rest of the UI a moment to settle.
    vTaskDelay(pdMS_TO_TICKS(1500));
    while (true) {
        if (s_cfg.weather_enabled) fetch_once();
        const uint32_t interval_ms = std::max<uint32_t>(5, s_cfg.weather_interval_min) * 60U * 1000U;
        // A task notification makes the "update now" button immediate.
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(interval_ms));
    }
}
}

bool weather_service_start(const ClockConfig& config) {
    s_cfg = config;
    if (!s_mutex) s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return false;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_status = WeatherStatus{};
    s_status.enabled = config.weather_enabled;
    xSemaphoreGive(s_mutex);

    if (s_task) return true;
    if (xTaskCreate(weather_task, "weather", 8192, nullptr, 5, &s_task) != pdPASS) {
        s_task = nullptr;
        return false;
    }
    ESP_LOGI(TAG, "Weather service %s; OpenWeather ZIP=%s,%s, refresh=%u min",
             config.weather_enabled ? "enabled" : "disabled",
             config.weather_zip, config.weather_country, config.weather_interval_min);
    return true;
}

void weather_service_get_status(WeatherStatus& status) {
    if (!s_mutex) {
        status = WeatherStatus{};
        return;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    status = s_status;
    xSemaphoreGive(s_mutex);
}

void weather_service_request_update() {
    if (s_task) xTaskNotifyGive(s_task);
}
