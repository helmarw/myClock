# Changelog

## 2.2.10

- Added direct `#RRGGBB` text entry for the clock color.
- Color picker, hex field, preview and presets remain synchronized.

## 2.2.9

- OTA now briefly shows `Updating`, then disables HUB75 output during flash writes.
- Normal display is restored if OTA fails before reboot.

## 2.2.8

- Added hardware-level OTA panel blanking using HUB75 OE.

## 2.2.7

- Added OTA display state and paused normal clock/weather drawing during updates.

## 2.2.6

- Fixed FreeRTOS main-task stack overflow in Wi-Fi scans by moving `wifi_ap_record_t` arrays to the heap.
- Applied the same heap-based handling to manual web scans.

## 2.2.5

- Hardened no-SSID setup mode.
- Avoided configuring the STA interface with an empty SSID.
- Replaced fatal checks in the setup path with logged errors.

## 2.2.4

- Improved AP+STA initialization for fallback/setup mode.
- STA and AP netifs are created before Wi-Fi driver initialization.

## 2.2.3

- Added strongest-BSSID selection for the configured SSID on boot and reconnect.
- Added full Wi-Fi scan diagnostics in the web interface.

## 2.2.x earlier porting work

- Native ESP-IDF 5.5.x port of the original matrix refresh and morphing-digit logic.
- Full display redraw after NTP sync to prevent incomplete startup digits.
- Web UI, NVS configuration, fallback AP and Web OTA.
- OpenWeather support and browser-assisted timezone configuration.
- DS18B20 and LDR support.
- Centered upper information row.
- Clock color presets, color picker and custom RGB color.
