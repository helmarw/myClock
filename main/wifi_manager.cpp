#include "wifi_manager.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

static const char* TAG = "wifi";
static EventGroupHandle_t s_events = nullptr;
static constexpr EventBits_t CONNECTED_BIT = BIT0;
static esp_netif_t* s_sta_netif = nullptr;
static esp_netif_t* s_ap_netif = nullptr;
static bool s_fallback_ap = false;
static char s_hostname[32] = "myClock";
static char s_ap_ssid[33] = "myClock";
static char s_configured_ssid[33] = "";
static char s_configured_password[65] = "";
static esp_event_handler_instance_t s_wifi_handler = nullptr;
static esp_event_handler_instance_t s_ip_handler = nullptr;
static volatile bool s_reconnect_task_running = false;
static bool s_wifi_started = false;

namespace {
void copy_string(char* dst, size_t dst_size, const char* src) {
    if (!dst || dst_size == 0) return;
    if (!src) src = "";
    std::strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

void format_bssid(char* dst, size_t dst_size, const uint8_t bssid[6]) {
    if (!dst || dst_size == 0 || !bssid) return;
    std::snprintf(dst, dst_size, "%02X:%02X:%02X:%02X:%02X:%02X",
                  bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
}

wifi_config_t make_base_sta_config() {
    wifi_config_t sta{};
    copy_string(reinterpret_cast<char*>(sta.sta.ssid), sizeof(sta.sta.ssid), s_configured_ssid);
    copy_string(reinterpret_cast<char*>(sta.sta.password), sizeof(sta.sta.password), s_configured_password);

    // Do not stop at the first matching AP. Search all channels and prefer the
    // matching BSSID with the strongest RSSI. This is also the fallback path
    // when the explicit pre-connect scan below cannot find the SSID.
    sta.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    sta.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    sta.sta.threshold.rssi = -127;
    sta.sta.threshold.authmode = WIFI_AUTH_OPEN;
    sta.sta.pmf_cfg.capable = true;
    sta.sta.pmf_cfg.required = false;
    return sta;
}

bool scan_best_matching_ap(wifi_ap_record_t& best) {
    if (!s_wifi_started || !s_configured_ssid[0]) return false;

    wifi_scan_config_t scan{};
    scan.show_hidden = true;
    scan.scan_type = WIFI_SCAN_TYPE_ACTIVE;

    ESP_LOGI(TAG, "Scanning for strongest AP matching SSID '%s'", s_configured_ssid);
    esp_err_t err = esp_wifi_scan_start(&scan, true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Best-AP scan failed: %s", esp_err_to_name(err));
        return false;
    }

    uint16_t total = 0;
    err = esp_wifi_scan_get_ap_num(&total);
    if (err != ESP_OK || total == 0) {
        if (err != ESP_OK) ESP_LOGW(TAG, "esp_wifi_scan_get_ap_num failed: %s", esp_err_to_name(err));
        return false;
    }

    constexpr uint16_t MAX_SCAN_RECORDS = 32;
    // IMPORTANT: wifi_ap_record_t is fairly large.  Keeping 32 records as a
    // local array used more than 3 KiB of the ESP-IDF main-task stack and
    // caused the observed "stack overflow in task main" during the boot scan.
    // Put the scan records on the heap instead.
    auto* aps = static_cast<wifi_ap_record_t*>(
        std::calloc(MAX_SCAN_RECORDS, sizeof(wifi_ap_record_t)));
    if (!aps) {
        ESP_LOGE(TAG, "Could not allocate memory for best-AP scan records");
        return false;
    }

    uint16_t count = std::min<uint16_t>(total, MAX_SCAN_RECORDS);
    err = esp_wifi_scan_get_ap_records(&count, aps);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_scan_get_ap_records failed: %s", esp_err_to_name(err));
        std::free(aps);
        return false;
    }

    bool found = false;
    for (uint16_t i = 0; i < count; ++i) {
        const char* ssid = reinterpret_cast<const char*>(aps[i].ssid);
        if (std::strncmp(ssid, s_configured_ssid, sizeof(aps[i].ssid)) != 0) continue;
        if (!found || aps[i].rssi > best.rssi) {
            best = aps[i];
            found = true;
        }
    }
    std::free(aps);

    if (found) {
        char bssid[18]{};
        format_bssid(bssid, sizeof(bssid), best.bssid);
        ESP_LOGI(TAG, "Best AP: BSSID=%s channel=%u RSSI=%d dBm",
                 bssid, best.primary, best.rssi);
    } else {
        ESP_LOGW(TAG, "SSID '%s' not found in scan", s_configured_ssid);
    }
    return found;
}

bool connect_to_best_ap() {
    if (!s_configured_ssid[0]) return false;

    wifi_config_t sta = make_base_sta_config();
    wifi_ap_record_t best{};
    if (scan_best_matching_ap(best)) {
        sta.sta.bssid_set = true;
        std::memcpy(sta.sta.bssid, best.bssid, sizeof(sta.sta.bssid));
        sta.sta.channel = best.primary;
    } else {
        // If the explicit scan fails (for example for a hidden SSID), leave
        // BSSID unlocked. ALL_CHANNEL_SCAN + SIGNAL sorting still gives the
        // driver the best chance to select the strongest matching AP.
        sta.sta.bssid_set = false;
        sta.sta.channel = 0;
        ESP_LOGW(TAG, "Falling back to ESP-IDF all-channel AP selection");
    }

    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &sta);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_config(STA) failed: %s", esp_err_to_name(err));
        return false;
    }

    err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

void reconnect_task(void*) {
    // Give the driver a short moment to finish processing the disconnect event.
    vTaskDelay(pdMS_TO_TICKS(750));
    if (!wifi_manager_is_connected() && s_configured_ssid[0]) {
        connect_to_best_ap();
    }
    s_reconnect_task_running = false;
    vTaskDelete(nullptr);
}

void schedule_reconnect_best() {
    if (s_reconnect_task_running || !s_configured_ssid[0]) return;
    s_reconnect_task_running = true;
    if (xTaskCreate(reconnect_task, "wifi_best_reconnect", 4096, nullptr, 5, nullptr) != pdPASS) {
        s_reconnect_task_running = false;
        ESP_LOGW(TAG, "Could not create best-AP reconnect task");
    }
}

void event_handler(void*, esp_event_base_t base, int32_t id, void* event_data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_events, CONNECTED_BIT);
        auto* event = static_cast<wifi_event_sta_disconnected_t*>(event_data);
        ESP_LOGW(TAG, "STA disconnected (reason=%u); rescanning for best matching AP", event ? event->reason : 0);
        schedule_reconnect_best();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        auto* event = static_cast<ip_event_got_ip_t*>(event_data);
        ESP_LOGI(TAG, "STA IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_events, CONNECTED_BIT);
    }
}

wifi_config_t make_ap_config(const ClockConfig& cfg) {
    wifi_config_t ap{};
    copy_string(reinterpret_cast<char*>(ap.ap.ssid), sizeof(ap.ap.ssid), s_ap_ssid);
    ap.ap.ssid_len = std::strlen(s_ap_ssid);
    ap.ap.channel = 1;
    ap.ap.max_connection = 4;

    const char* ap_password = std::strlen(cfg.admin_password) >= 8 ? cfg.admin_password : "ChangeMe";
    copy_string(reinterpret_cast<char*>(ap.ap.password), sizeof(ap.ap.password), ap_password);
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    return ap;
}

bool start_fallback_ap(const ClockConfig& cfg) {
    // The AP netif is deliberately created before esp_wifi_init() in
    // wifi_manager_start().  Creating it only after the Wi-Fi driver was
    // already started can leave the default Wi-Fi/netif handlers in an
    // inconsistent state on ESP-IDF 5.x.
    if (!s_ap_netif) {
        ESP_LOGE(TAG, "AP netif is not initialized");
        return false;
    }

    wifi_config_t ap = make_ap_config(cfg);

    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_mode(APSTA) failed: %s", esp_err_to_name(err));
        return false;
    }
    err = esp_wifi_set_config(WIFI_IF_AP, &ap);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_config(AP) failed: %s", esp_err_to_name(err));
        return false;
    }

    s_fallback_ap = true;
    ESP_LOGW(TAG, "Fallback AP active: SSID='%s', IP=192.168.4.1", s_ap_ssid);
    ESP_LOGW(TAG, "Fallback AP password is the web admin password (or 'ChangeMe' if shorter than 8 chars)");
    return true;
}
}  // namespace

bool wifi_manager_start(const ClockConfig& cfg) {
    if (!s_events) s_events = xEventGroupCreate();
    copy_string(s_configured_ssid, sizeof(s_configured_ssid), cfg.wifi_ssid);
    copy_string(s_configured_password, sizeof(s_configured_password), cfg.wifi_password);

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(err));
        return false;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "event loop create failed: %s", esp_err_to_name(err));
        return false;
    }

    // Create both default netifs BEFORE esp_wifi_init().  This is required for
    // a robust AP+STA fallback path and also lets the setup AP keep STA scan
    // capability so the web UI can list nearby SSIDs while no SSID is saved.
    if (!s_sta_netif) s_sta_netif = esp_netif_create_default_wifi_sta();
    if (!s_ap_netif) s_ap_netif = esp_netif_create_default_wifi_ap();
    if (!s_sta_netif || !s_ap_netif) {
        ESP_LOGE(TAG, "Could not create STA/AP netifs");
        return false;
    }

    uint8_t mac[6]{};
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));
    std::snprintf(s_hostname, sizeof(s_hostname), "myClock-%02X%02X%02X", mac[3], mac[4], mac[5]);
    std::snprintf(s_ap_ssid, sizeof(s_ap_ssid), "%s", s_hostname);
    ESP_ERROR_CHECK(esp_netif_set_hostname(s_sta_netif, s_hostname));

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                         &event_handler, nullptr, &s_wifi_handler));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                         &event_handler, nullptr, &s_ip_handler));

    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    if (!cfg.wifi_ssid[0]) {
        // Setup mode: do NOT apply an empty STA configuration.  ESP-IDF may
        // reject an empty station SSID; using ESP_ERROR_CHECK here would then
        // abort() and cause exactly the observed SW_CPU_RESET boot loop.
        // AP+STA mode is still used so the STA interface exists for manual
        // scans from the web UI, but it is left unconfigured and never asked
        // to connect until the user saves an SSID.
        wifi_config_t ap = make_ap_config(cfg);

        ESP_LOGI(TAG, "Setup mode: no STA credentials configured");
        err = esp_wifi_set_mode(WIFI_MODE_APSTA);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_wifi_set_mode(APSTA) failed: %s", esp_err_to_name(err));
            return false;
        }
        err = esp_wifi_set_config(WIFI_IF_AP, &ap);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_wifi_set_config(AP) failed: %s", esp_err_to_name(err));
            return false;
        }
        err = esp_wifi_start();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_wifi_start(APSTA setup mode) failed: %s", esp_err_to_name(err));
            return false;
        }

        s_wifi_started = true;
        s_fallback_ap = true;
        ESP_LOGW(TAG, "No WiFi SSID configured; setup AP active: SSID='%s', IP=192.168.4.1", s_ap_ssid);
        ESP_LOGW(TAG, "Setup AP password is the web admin password (or 'ChangeMe' if shorter than 8 chars)");
        return false;
    }

    // Normal station mode.  If association later times out, start_fallback_ap()
    // switches the already initialized driver to AP+STA mode.
    wifi_config_t sta = make_base_sta_config();
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
    ESP_ERROR_CHECK(esp_wifi_start());
    s_wifi_started = true;

    ESP_LOGI(TAG, "Connecting to SSID '%s' as %s using strongest matching AP", cfg.wifi_ssid, s_hostname);
    connect_to_best_ap();

    EventBits_t bits = xEventGroupWaitBits(s_events, CONNECTED_BIT, pdFALSE, pdTRUE,
                                           pdMS_TO_TICKS(20000));
    if (bits & CONNECTED_BIT) {
        wifi_ap_record_t ap{};
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            ESP_LOGI(TAG, "Connected: BSSID=%02x:%02x:%02x:%02x:%02x:%02x channel=%u RSSI=%d dBm",
                     ap.bssid[0], ap.bssid[1], ap.bssid[2], ap.bssid[3], ap.bssid[4], ap.bssid[5],
                     ap.primary, ap.rssi);
        }
        return true;
    }

    ESP_LOGW(TAG, "Could not connect within 20 s; enabling fallback AP");
    start_fallback_ap(cfg);
    return false;
}

bool wifi_manager_is_connected() {
    return s_events && (xEventGroupGetBits(s_events) & CONNECTED_BIT);
}

const char* wifi_manager_hostname() {
    return s_hostname;
}

void wifi_manager_get_status(WifiStatus& status) {
    status = WifiStatus{};
    copy_string(status.hostname, sizeof(status.hostname), s_hostname);
    copy_string(status.ssid, sizeof(status.ssid), s_configured_ssid);
    status.fallback_ap = s_fallback_ap;
    if (s_fallback_ap) {
        copy_string(status.ap_ssid, sizeof(status.ap_ssid), s_ap_ssid);
        copy_string(status.ap_ip, sizeof(status.ap_ip), "192.168.4.1");
    }

    status.sta_connected = wifi_manager_is_connected();
    if (!status.sta_connected) return;

    wifi_ap_record_t ap{};
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        copy_string(status.ssid, sizeof(status.ssid), reinterpret_cast<const char*>(ap.ssid));
        status.rssi = ap.rssi;
        status.channel = ap.primary;
        format_bssid(status.bssid, sizeof(status.bssid), ap.bssid);
    }

    if (s_sta_netif) {
        esp_netif_ip_info_t ip{};
        if (esp_netif_get_ip_info(s_sta_netif, &ip) == ESP_OK) {
            std::snprintf(status.ip, sizeof(status.ip), IPSTR, IP2STR(&ip.ip));
        }
    }
}

int wifi_manager_scan(WifiScanResult* results, std::size_t max_results) {
    if (!results || max_results == 0) return 0;

    wifi_scan_config_t scan{};
    scan.show_hidden = true;
    scan.scan_type = WIFI_SCAN_TYPE_ACTIVE;

    ESP_LOGI(TAG, "Starting Wi-Fi scan");
    esp_err_t err = esp_wifi_scan_start(&scan, true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi scan failed to start/complete: %s", esp_err_to_name(err));
        return -1;
    }

    uint16_t total = 0;
    err = esp_wifi_scan_get_ap_num(&total);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_scan_get_ap_num failed: %s", esp_err_to_name(err));
        return -1;
    }

    constexpr uint16_t MAX_SCAN_RECORDS = 32;
    uint16_t count = static_cast<uint16_t>(std::min<std::size_t>(
        std::min<std::size_t>(total, MAX_SCAN_RECORDS), max_results));

    if (count == 0) {
        ESP_LOGI(TAG, "Wi-Fi scan finished: no access points found");
        return 0;
    }

    // Use heap storage here as well.  The same 32-record stack allocation could
    // otherwise overflow the HTTP server task when a manual scan is requested.
    auto* aps = static_cast<wifi_ap_record_t*>(
        std::calloc(count, sizeof(wifi_ap_record_t)));
    if (!aps) {
        ESP_LOGE(TAG, "Could not allocate memory for Wi-Fi scan records");
        return -1;
    }

    err = esp_wifi_scan_get_ap_records(&count, aps);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_scan_get_ap_records failed: %s", esp_err_to_name(err));
        std::free(aps);
        return -1;
    }

    for (uint16_t i = 0; i < count; ++i) {
        WifiScanResult& out = results[i];
        copy_string(out.ssid, sizeof(out.ssid), reinterpret_cast<const char*>(aps[i].ssid));
        format_bssid(out.bssid, sizeof(out.bssid), aps[i].bssid);
        out.rssi = aps[i].rssi;
        out.channel = aps[i].primary;
    }
    std::free(aps);

    std::sort(results, results + count, [](const WifiScanResult& a, const WifiScanResult& b) {
        return a.rssi > b.rssi;
    });

    ESP_LOGI(TAG, "Wi-Fi scan finished: %u APs returned (%u seen)", count, total);
    return static_cast<int>(count);
}
