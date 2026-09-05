#include "sensor_service.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "app_config.hpp"
#include "ds18b20.h"
#include "onewire_bus.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char* TAG = "sensors";

namespace {
ClockConfig s_config{};
SensorStatus s_status{};
SemaphoreHandle_t s_mutex = nullptr;
onewire_bus_handle_t s_onewire_bus = nullptr;
ds18b20_device_handle_t s_ds18b20 = nullptr;
adc_oneshot_unit_handle_t s_adc = nullptr;

void set_error(char* dst, size_t size, const char* msg) {
    if (!dst || size == 0) return;
    std::snprintf(dst, size, "%s", msg ? msg : "");
}

void commit_status(const SensorStatus& st) {
    if (!s_mutex) return;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        s_status = st;
        xSemaphoreGive(s_mutex);
    }
}

bool init_ldr(SensorStatus& st) {
    if (!s_config.ldr_enabled) return true;

    adc_oneshot_unit_init_cfg_t unit_cfg{};
    unit_cfg.unit_id = ADC_UNIT_1;
    unit_cfg.ulp_mode = ADC_ULP_MODE_DISABLE;
    esp_err_t err = adc_oneshot_new_unit(&unit_cfg, &s_adc);
    if (err != ESP_OK) {
        char e[80];
        std::snprintf(e, sizeof(e), "ADC init: %s", esp_err_to_name(err));
        set_error(st.ldr_error, sizeof(st.ldr_error), e);
        ESP_LOGE(TAG, "%s", e);
        return false;
    }

    adc_oneshot_chan_cfg_t chan_cfg{};
    chan_cfg.atten = ADC_ATTEN_DB_12;
    chan_cfg.bitwidth = ADC_BITWIDTH_12;
    err = adc_oneshot_config_channel(s_adc, ADC_CHANNEL_0, &chan_cfg); // GPIO36 / A0
    if (err != ESP_OK) {
        char e[80];
        std::snprintf(e, sizeof(e), "ADC channel: %s", esp_err_to_name(err));
        set_error(st.ldr_error, sizeof(st.ldr_error), e);
        ESP_LOGE(TAG, "%s", e);
        return false;
    }

    ESP_LOGI(TAG, "LDR enabled on A0 / GPIO%d, threshold=%u", MYCLOCK_LDR_GPIO, s_config.ldr_threshold);
    return true;
}

bool init_ds18b20(SensorStatus& st) {
    if (!s_config.ds18_enabled) return true;

    onewire_bus_config_t bus_cfg{};
    bus_cfg.bus_gpio_num = MYCLOCK_DS18B20_GPIO;
    onewire_bus_rmt_config_t rmt_cfg{};
    rmt_cfg.max_rx_bytes = 10;

    esp_err_t err = onewire_new_bus_rmt(&bus_cfg, &rmt_cfg, &s_onewire_bus);
    if (err != ESP_OK) {
        char e[80];
        std::snprintf(e, sizeof(e), "1-Wire init: %s", esp_err_to_name(err));
        set_error(st.ds18_error, sizeof(st.ds18_error), e);
        ESP_LOGE(TAG, "%s", e);
        return false;
    }

    onewire_device_iter_handle_t iter = nullptr;
    err = onewire_new_device_iter(s_onewire_bus, &iter);
    if (err != ESP_OK) {
        char e[80];
        std::snprintf(e, sizeof(e), "1-Wire search: %s", esp_err_to_name(err));
        set_error(st.ds18_error, sizeof(st.ds18_error), e);
        ESP_LOGE(TAG, "%s", e);
        return false;
    }

    onewire_device_t dev{};
    esp_err_t search = ESP_OK;
    while ((search = onewire_device_iter_get_next(iter, &dev)) == ESP_OK) {
        ds18b20_config_t ds_cfg{};
        if (ds18b20_new_device(&dev, &ds_cfg, &s_ds18b20) == ESP_OK) {
            st.ds18_found = true;
            st.ds18_error[0] = '\0';
            ESP_LOGI(TAG, "DS18B20 found on GPIO%d, address=%016llX",
                     MYCLOCK_DS18B20_GPIO, static_cast<unsigned long long>(dev.address));
            break;
        }
    }
    onewire_del_device_iter(iter);

    if (!st.ds18_found) {
        set_error(st.ds18_error, sizeof(st.ds18_error), "kein DS18B20 gefunden");
        ESP_LOGW(TAG, "No DS18B20 found on GPIO%d", MYCLOCK_DS18B20_GPIO);
        return false;
    }
    return true;
}

uint8_t ldr_brightness(int filtered) {
    const int threshold = s_config.ldr_threshold > 0 ? s_config.ldr_threshold : 1000;
    const uint8_t b = s_config.brightness;
    if (filtered >= threshold) return b;
    if (filtered < (threshold >> 3)) return static_cast<uint8_t>(b >> 4);
    if (filtered < (threshold >> 2)) return static_cast<uint8_t>(b >> 3);
    if (filtered < (threshold >> 1)) return static_cast<uint8_t>(b >> 2);
    return static_cast<uint8_t>(b >> 1);
}

void sensor_task(void*) {
    SensorStatus st{};
    st.ds18_enabled = s_config.ds18_enabled;
    st.ldr_enabled = s_config.ldr_enabled;
    st.effective_brightness = s_config.brightness;
    st.ldr_filtered = s_config.ldr_threshold;

    init_ldr(st);
    init_ds18b20(st);
    ++st.sequence;
    commit_status(st);

    int seconds = 0;
    while (true) {
        bool changed = false;

        if (s_config.ldr_enabled && s_adc) {
            int raw = 0;
            const esp_err_t err = adc_oneshot_read(s_adc, ADC_CHANNEL_0, &raw);
            if (err == ESP_OK && raw > 20) { // same invalid-value guard as the original code
                st.ldr_valid = true;
                st.ldr_raw = raw;
                st.ldr_filtered = (st.ldr_filtered * 3 + raw) >> 2;
                st.effective_brightness = ldr_brightness(st.ldr_filtered);
                st.ldr_error[0] = '\0';
                changed = true;
            } else if (err != ESP_OK) {
                st.ldr_valid = false;
                st.effective_brightness = s_config.brightness;
                char e[80];
                std::snprintf(e, sizeof(e), "ADC read: %s", esp_err_to_name(err));
                set_error(st.ldr_error, sizeof(st.ldr_error), e);
                changed = true;
            }
        } else {
            st.effective_brightness = s_config.brightness;
        }

        // Original firmware queried the DS18B20 every 10 seconds.
        if (s_config.ds18_enabled && s_ds18b20 && (seconds % 10 == 0)) {
            float temp_c = 0.0f;
            esp_err_t err = ds18b20_trigger_temperature_conversion(s_ds18b20);
            if (err == ESP_OK) err = ds18b20_get_temperature(s_ds18b20, &temp_c);
            if (err == ESP_OK && std::isfinite(temp_c) && temp_c >= -55.0f && temp_c <= 125.0f) {
                st.ds18_valid = true;
                st.ds18_temperature_c = temp_c;
                st.ds18_error[0] = '\0';
                changed = true;
                ESP_LOGI(TAG, "DS18B20: %.2f C", temp_c);
            } else {
                st.ds18_valid = false;
                char e[80];
                if (err == ESP_OK) std::snprintf(e, sizeof(e), "ungueltiger Messwert %.2f C", temp_c);
                else std::snprintf(e, sizeof(e), "Messfehler: %s", esp_err_to_name(err));
                set_error(st.ds18_error, sizeof(st.ds18_error), e);
                changed = true;
                ESP_LOGW(TAG, "DS18B20 %s", e);
            }
        }

        if (changed) {
            ++st.sequence;
            commit_status(st);
        }
        ++seconds;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
} // namespace

bool sensor_service_start(const ClockConfig& config) {
    s_config = config;
    if (!s_mutex) s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        ESP_LOGE(TAG, "Could not create sensor mutex");
        return false;
    }

    s_status = SensorStatus{};
    s_status.ds18_enabled = config.ds18_enabled;
    s_status.ldr_enabled = config.ldr_enabled;
    s_status.effective_brightness = config.brightness;

    if (xTaskCreate(sensor_task, "sensors", 6144, nullptr, 5, nullptr) != pdPASS) {
        ESP_LOGE(TAG, "Could not start sensor task");
        return false;
    }
    return true;
}

void sensor_service_get_status(SensorStatus& out) {
    if (!s_mutex) {
        out = s_status;
        return;
    }
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        out = s_status;
        xSemaphoreGive(s_mutex);
    } else {
        out = s_status;
    }
}
