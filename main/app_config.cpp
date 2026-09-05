#include "app_config.hpp"
#include "clock_theme.hpp"

#include <cstring>

#include "esp_log.h"
#include "nvs.h"

static const char* TAG = "config";
static constexpr const char* NVS_NS = "myclock";

namespace {
void copy_string(char* dst, size_t dst_size, const char* src) {
    if (!dst || dst_size == 0) return;
    if (!src) src = "";
    std::strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

void load_str(nvs_handle_t h, const char* key, char* dst, size_t dst_size) {
    size_t len = dst_size;
    const esp_err_t err = nvs_get_str(h, key, dst, &len);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Could not read %s: %s", key, esp_err_to_name(err));
    }
}
}

void config_set_defaults(ClockConfig& cfg) {
    cfg = ClockConfig{};
    copy_string(cfg.wifi_ssid, sizeof(cfg.wifi_ssid), CONFIG_MYCLOCK_WIFI_SSID);
    copy_string(cfg.wifi_password, sizeof(cfg.wifi_password), CONFIG_MYCLOCK_WIFI_PASSWORD);
    copy_string(cfg.admin_password, sizeof(cfg.admin_password), CONFIG_MYCLOCK_ADMIN_PASSWORD);

    copy_string(cfg.timezone_name, sizeof(cfg.timezone_name), "Europe/Berlin");
    copy_string(cfg.timezone, sizeof(cfg.timezone), CONFIG_MYCLOCK_TZ);
    cfg.timezone_auto = true;

    cfg.brightness = static_cast<uint8_t>(CONFIG_MYCLOCK_BRIGHTNESS);
    cfg.clock_theme = 0;
    cfg.clock_r = 32;
    cfg.clock_g = 64;
    cfg.clock_b = 160;
#if CONFIG_MYCLOCK_24H
    cfg.use_24h = true;
#else
    cfg.use_24h = false;
#endif

    cfg.weather_enabled = false;
    copy_string(cfg.openweather_key, sizeof(cfg.openweather_key), CONFIG_MYCLOCK_OPENWEATHER_KEY);
    copy_string(cfg.weather_zip, sizeof(cfg.weather_zip), CONFIG_MYCLOCK_WEATHER_ZIP);
    copy_string(cfg.weather_country, sizeof(cfg.weather_country), CONFIG_MYCLOCK_WEATHER_COUNTRY);
    copy_string(cfg.weather_language, sizeof(cfg.weather_language), CONFIG_MYCLOCK_WEATHER_LANGUAGE);
#if CONFIG_MYCLOCK_WEATHER_CELSIUS
    cfg.weather_celsius = true;
#else
    cfg.weather_celsius = false;
#endif
    cfg.weather_interval_min = static_cast<uint16_t>(CONFIG_MYCLOCK_WEATHER_INTERVAL_MIN);
    if (cfg.openweather_key[0] && cfg.weather_zip[0]) cfg.weather_enabled = true;

    cfg.ds18_enabled = true;
    cfg.ds18_temp_offset = 0;
    cfg.ldr_enabled = true;
    cfg.ldr_threshold = 1000;
}

bool config_load(ClockConfig& cfg) {
    config_set_defaults(cfg);

    nvs_handle_t h{};
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No saved myClock config yet; using menuconfig defaults");
        return true;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return false;
    }

    load_str(h, "wifi_ssid", cfg.wifi_ssid, sizeof(cfg.wifi_ssid));
    load_str(h, "wifi_pass", cfg.wifi_password, sizeof(cfg.wifi_password));
    load_str(h, "admin_pass", cfg.admin_password, sizeof(cfg.admin_password));

    // Stage 2 already stored the POSIX string under key "tz". Keep loading it
    // so upgrades do not lose the working time-zone setting.
    load_str(h, "tz", cfg.timezone, sizeof(cfg.timezone));
    load_str(h, "tz_name", cfg.timezone_name, sizeof(cfg.timezone_name));

    load_str(h, "ow_key", cfg.openweather_key, sizeof(cfg.openweather_key));
    load_str(h, "w_zip", cfg.weather_zip, sizeof(cfg.weather_zip));
    load_str(h, "w_country", cfg.weather_country, sizeof(cfg.weather_country));
    load_str(h, "w_lang", cfg.weather_language, sizeof(cfg.weather_language));

    uint8_t bright = cfg.brightness;
    if (nvs_get_u8(h, "bright", &bright) == ESP_OK) cfg.brightness = bright;
    uint8_t theme = cfg.clock_theme;
    if (nvs_get_u8(h, "clock_theme", &theme) == ESP_OK) cfg.clock_theme = theme <= 9 ? theme : 0;

    // Backward-compatible migration from the Stage-3 preset-only setting:
    // first derive RGB from the saved preset, then override with explicit RGB
    // values if a newer firmware has already stored them.
    const ClockThemeChoice& saved_theme = clock_theme_by_id(cfg.clock_theme);
    cfg.clock_r = saved_theme.r;
    cfg.clock_g = saved_theme.g;
    cfg.clock_b = saved_theme.b;
    uint8_t clock_r = cfg.clock_r;
    uint8_t clock_g = cfg.clock_g;
    uint8_t clock_b = cfg.clock_b;
    if (nvs_get_u8(h, "clock_r", &clock_r) == ESP_OK) cfg.clock_r = clock_r;
    if (nvs_get_u8(h, "clock_g", &clock_g) == ESP_OK) cfg.clock_g = clock_g;
    if (nvs_get_u8(h, "clock_b", &clock_b) == ESP_OK) cfg.clock_b = clock_b;
    cfg.clock_theme = clock_theme_id_for_rgb(cfg.clock_r, cfg.clock_g, cfg.clock_b);
    uint8_t h24 = cfg.use_24h ? 1 : 0;
    if (nvs_get_u8(h, "h24", &h24) == ESP_OK) cfg.use_24h = h24 != 0;
    uint8_t tz_auto = cfg.timezone_auto ? 1 : 0;
    if (nvs_get_u8(h, "tz_auto", &tz_auto) == ESP_OK) cfg.timezone_auto = tz_auto != 0;
    uint8_t weather_enabled = cfg.weather_enabled ? 1 : 0;
    if (nvs_get_u8(h, "w_enabled", &weather_enabled) == ESP_OK) cfg.weather_enabled = weather_enabled != 0;
    uint8_t celsius = cfg.weather_celsius ? 1 : 0;
    if (nvs_get_u8(h, "w_celsius", &celsius) == ESP_OK) cfg.weather_celsius = celsius != 0;
    uint16_t interval = cfg.weather_interval_min;
    if (nvs_get_u16(h, "w_interval", &interval) == ESP_OK) cfg.weather_interval_min = interval;

    uint8_t ds18_enabled = cfg.ds18_enabled ? 1 : 0;
    if (nvs_get_u8(h, "ds18_en", &ds18_enabled) == ESP_OK) cfg.ds18_enabled = ds18_enabled != 0;
    int8_t temp_offset = cfg.ds18_temp_offset;
    if (nvs_get_i8(h, "ds18_off", &temp_offset) == ESP_OK) cfg.ds18_temp_offset = temp_offset;
    uint8_t ldr_enabled = cfg.ldr_enabled ? 1 : 0;
    if (nvs_get_u8(h, "ldr_en", &ldr_enabled) == ESP_OK) cfg.ldr_enabled = ldr_enabled != 0;
    uint16_t ldr_threshold = cfg.ldr_threshold;
    if (nvs_get_u16(h, "ldr_thr", &ldr_threshold) == ESP_OK) cfg.ldr_threshold = ldr_threshold;

    nvs_close(h);
    ESP_LOGI(TAG,
             "Loaded config from NVS (SSID='%s', brightness=%u, color=#%02X%02X%02X, theme=%u, %s, TZ=%s [%s], weather=%s)",
             cfg.wifi_ssid, cfg.brightness, cfg.clock_r, cfg.clock_g, cfg.clock_b, cfg.clock_theme, cfg.use_24h ? "24h" : "12h",
             cfg.timezone_name, cfg.timezone, cfg.weather_enabled ? "on" : "off");
    ESP_LOGI(TAG, "Sensors: DS18B20=%s offset=%d, LDR=%s threshold=%u",
             cfg.ds18_enabled ? "on" : "off", cfg.ds18_temp_offset,
             cfg.ldr_enabled ? "on" : "off", cfg.ldr_threshold);
    return true;
}

bool config_save(const ClockConfig& cfg) {
    nvs_handle_t h{};
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open for write failed: %s", esp_err_to_name(err));
        return false;
    }

    if ((err = nvs_set_str(h, "wifi_ssid", cfg.wifi_ssid)) != ESP_OK ||
        (err = nvs_set_str(h, "wifi_pass", cfg.wifi_password)) != ESP_OK ||
        (err = nvs_set_str(h, "admin_pass", cfg.admin_password)) != ESP_OK ||
        (err = nvs_set_str(h, "tz", cfg.timezone)) != ESP_OK ||
        (err = nvs_set_str(h, "tz_name", cfg.timezone_name)) != ESP_OK ||
        (err = nvs_set_u8(h, "tz_auto", cfg.timezone_auto ? 1 : 0)) != ESP_OK ||
        (err = nvs_set_u8(h, "bright", cfg.brightness)) != ESP_OK ||
        (err = nvs_set_u8(h, "clock_theme", cfg.clock_theme)) != ESP_OK ||
        (err = nvs_set_u8(h, "clock_r", cfg.clock_r)) != ESP_OK ||
        (err = nvs_set_u8(h, "clock_g", cfg.clock_g)) != ESP_OK ||
        (err = nvs_set_u8(h, "clock_b", cfg.clock_b)) != ESP_OK ||
        (err = nvs_set_u8(h, "h24", cfg.use_24h ? 1 : 0)) != ESP_OK ||
        (err = nvs_set_u8(h, "w_enabled", cfg.weather_enabled ? 1 : 0)) != ESP_OK ||
        (err = nvs_set_str(h, "ow_key", cfg.openweather_key)) != ESP_OK ||
        (err = nvs_set_str(h, "w_zip", cfg.weather_zip)) != ESP_OK ||
        (err = nvs_set_str(h, "w_country", cfg.weather_country)) != ESP_OK ||
        (err = nvs_set_str(h, "w_lang", cfg.weather_language)) != ESP_OK ||
        (err = nvs_set_u8(h, "w_celsius", cfg.weather_celsius ? 1 : 0)) != ESP_OK ||
        (err = nvs_set_u16(h, "w_interval", cfg.weather_interval_min)) != ESP_OK ||
        (err = nvs_set_u8(h, "ds18_en", cfg.ds18_enabled ? 1 : 0)) != ESP_OK ||
        (err = nvs_set_i8(h, "ds18_off", cfg.ds18_temp_offset)) != ESP_OK ||
        (err = nvs_set_u8(h, "ldr_en", cfg.ldr_enabled ? 1 : 0)) != ESP_OK ||
        (err = nvs_set_u16(h, "ldr_thr", cfg.ldr_threshold)) != ESP_OK ||
        (err = nvs_commit(h)) != ESP_OK) {
        ESP_LOGE(TAG, "Saving config failed: %s", esp_err_to_name(err));
        nvs_close(h);
        return false;
    }

    nvs_close(h);
    ESP_LOGI(TAG, "Configuration saved to NVS");
    return true;
}
