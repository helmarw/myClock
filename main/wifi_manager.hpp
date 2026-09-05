#pragma once

#include <cstddef>
#include <cstdint>
#include "app_config.hpp"

struct WifiStatus {
    bool sta_connected = false;
    bool fallback_ap = false;
    char hostname[32]{};
    char ssid[33]{};
    char ip[16]{};
    char bssid[18]{};
    int rssi = 0;
    int channel = 0;
    char ap_ssid[33]{};
    char ap_ip[16]{};
};

struct WifiScanResult {
    char ssid[33]{};
    char bssid[18]{};
    int rssi = 0;
    int channel = 0;
};

bool wifi_manager_start(const ClockConfig& cfg);
bool wifi_manager_is_connected();
void wifi_manager_get_status(WifiStatus& status);
// Performs a synchronous Wi-Fi scan and returns results sorted by RSSI (best first).
// Returns the number of results, or -1 if the scan could not be started/completed.
int wifi_manager_scan(WifiScanResult* results, std::size_t max_results);
const char* wifi_manager_hostname();
