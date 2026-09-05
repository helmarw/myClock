#include "wifi_time.hpp"

#include <cstdlib>
#include <ctime>

#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "freertos/FreeRTOS.h"

static const char* TAG = "time";

void configure_timezone(const char* timezone) {
    if (!timezone || !timezone[0]) timezone = "CET-1CEST,M3.5.0,M10.5.0/3";
    setenv("TZ", timezone, 1);
    tzset();
    ESP_LOGI(TAG, "TZ=%s", timezone);
}

bool sync_time_from_ntp(const char* timezone) {
    configure_timezone(timezone);

    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(
        2, ESP_SNTP_SERVER_LIST("2.pool.ntp.org", "1.pool.ntp.org"));
    const esp_err_t init_err = esp_netif_sntp_init(&config);
    if (init_err != ESP_OK && init_err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "SNTP init failed: %s", esp_err_to_name(init_err));
        return false;
    }

    const esp_err_t err = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(15000));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SNTP sync timeout: %s", esp_err_to_name(err));
        return false;
    }

    std::time_t now = std::time(nullptr);
    struct tm ti{};
    localtime_r(&now, &ti);
    char buf[64]{};
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %Z", &ti);
    ESP_LOGI(TAG, "Time synchronized: %s", buf);
    return true;
}
