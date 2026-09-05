# myClock for ESP-IDF

Modern ESP-IDF port of **myClock**, a 64×32 HUB75 LED matrix clock with morphing digits, weather information, local sensors, web configuration and OTA updates.

This version is intended for the existing **ESP-WROOM-32 / MH-ET LIVE D1 mini** hardware used by the original project and has been developed against **ESP-IDF 5.5.4**.

## Features

- 64×32 P4 HUB75 LED matrix
- Morphing HH:MM:SS clock display
- Full display redraw after NTP synchronization
- 12/24-hour mode
- NTP time synchronization
- Browser-assisted timezone selection, with `Europe/Berlin` as default
- No TimeZoneDB API key required
- OpenWeather current weather display
- Configurable OpenWeather API key, postal code, country, language and refresh interval
- DS18B20 indoor temperature sensor
- LDR-based automatic display brightness
- Configurable clock color using presets, a color picker, or a direct `#RRGGBB` value
- Centered upper information row
- Web configuration with HTTP Basic Authentication
- Wi-Fi scan listing all visible SSIDs/BSSIDs
- Automatic selection of the strongest access point with the configured SSID on boot/reconnect
- Setup/fallback access point if no usable Wi-Fi configuration is available
- Web OTA firmware update
- OTA feedback: briefly shows `Updating`, then blanks the panel during flash writes to avoid distracting flicker
- Persistent settings in NVS

## Hardware / GPIO assignment

The current firmware preserves the GPIO assignment of the working hardware. Do not change these values unless the matrix wiring is changed as well.

| Function | GPIO |
|---|---:|
| HUB75 LAT | 26 |
| HUB75 A | 22 |
| HUB75 B | 21 |
| HUB75 C | 5 |
| HUB75 D | 19 |
| HUB75 OE | 16 |
| HUB75 CLK | 18 |
| HUB75 MOSI/data | 23 |
| DS18B20 | 17 |
| LDR / A0 | 36 |

The matrix driver uses the original project's SPI/multiplex approach rather than replacing the hardware interface with a different HUB75 DMA pinout.

## Requirements

- ESP-IDF **5.5.x** (developed/tested with 5.5.4)
- ESP32 target
- 4 MB flash
- Internet connection during the first build so the ESP-IDF Component Manager can fetch:
  - `espressif/ds18b20`
  - `espressif/onewire_bus`

## Build

Clone/download the repository and open an ESP-IDF shell:

```bash
idf.py set-target esp32
idf.py menuconfig
idf.py build
```

Optional initial values can be entered under **Component config → myClock**. They are only intended for the first boot; settings can later be changed from the web interface and are stored in NVS.

Flash by USB:

```bash
idf.py -p /dev/cu.YOURPORT flash monitor
```

On Linux the serial port is commonly something such as `/dev/ttyUSB0` or `/dev/ttyACM0`.

## First start / setup AP

If no Wi-Fi SSID is configured, myClock starts a local setup access point:

```text
SSID: myClock-XXXXXX
IP:   192.168.4.1
```

Initial credentials:

```text
Web user:     admin
Web password: ChangeMe
AP password:  ChangeMe
```

Open `http://192.168.4.1/`, configure Wi-Fi and change the admin password.

> **Security:** `ChangeMe` is intentionally only a first-start default. Change the admin password before leaving the device on an untrusted network.

If a configured Wi-Fi cannot be reached, the fallback AP can also be used for recovery/configuration.

## Wi-Fi / strongest AP selection

The web interface can scan and display all visible access points, including:

- SSID
- BSSID
- channel
- RSSI
- current AP
- strongest AP with the configured SSID

When connecting, myClock scans for APs whose SSID exactly matches the configured SSID and selects the candidate with the strongest RSSI. The selection is repeated after a disconnect.

This is deliberately **not continuous roaming**: while a connection remains stable, myClock stays on the current AP instead of repeatedly switching for small RSSI differences.

Example log output:

```text
I (...) wifi: Scanning for strongest AP matching SSID 'MyWiFi'
I (...) wifi: Best AP: BSSID=AA:BB:CC:DD:EE:FF channel=6 RSSI=-52 dBm
I (...) wifi: Connected: BSSID=AA:BB:CC:DD:EE:FF channel=6 RSSI=-53 dBm
```

## Timezone

The web interface can take the browser's IANA timezone, for example `Europe/Berlin`, and maps supported zones to the POSIX timezone string used by ESP-IDF/newlib.

The default is:

```text
Europe/Berlin
CET-1CEST,M3.5.0,M10.5.0/3
```

This handles CET/CEST automatically and removes the need for the old TimeZoneDB API request.

## Weather

Weather uses the OpenWeather current-weather API. Configure these values in the web interface:

- API key
- postal code
- country code
- language
- metric/imperial units
- refresh interval

The clock continues to operate normally if weather is disabled or no valid API key is configured.

## Sensors

### DS18B20

- GPIO17
- indoor temperature shown in the upper information row
- configurable temperature offset

### LDR

- A0 / GPIO36
- automatic display brightness
- configurable threshold
- smoothed readings to reduce brightness jitter

## Clock color

The large time digits and colons can be configured independently of the weather/status colors.

Available controls:

- preset themes
- browser color picker
- direct hexadecimal RGB entry, e.g. `#FF4C04`

Because the matrix refresh method has limited per-channel intensity levels, the physical LED color can differ somewhat from the browser preview.

## OTA update

Build a new firmware image:

```bash
idf.py build
```

Upload this file in the web interface:

```text
build/myClock_idf.bin
```

At the start of OTA the matrix briefly displays `Updating`. Before flash erase/write begins, HUB75 output is disabled via OE and the matrix remains dark until reboot. This avoids the irregular-looking refresh flicker caused by flash operations.

## Repository hygiene / credentials

The repository intentionally includes `sdkconfig.defaults`, but **does not include `sdkconfig`**.

`menuconfig` can write Wi-Fi credentials and API keys into the local `sdkconfig`, so `.gitignore` excludes:

```text
sdkconfig
sdkconfig.old
build/
managed_components/
```

Before pushing a public fork, check staged files with:

```bash
git status
git diff --cached
```

Do not commit real Wi-Fi passwords, admin passwords or API keys.

## Project history and credits

This ESP-IDF version is based on the `helmarw/myClock` project and preserves the original morphing-clock concept and hardware behavior.

Original myClock author:

- **David M Denney (dragondaud)**

Original project:

- `https://github.com/dragondaud/myClock`

ESP-IDF port source/history:

- `https://github.com/helmarw/myClock`

The original project is distributed under the **MIT License**. The original copyright and license text are retained in [`LICENSE`](LICENSE).

## License

MIT — see [`LICENSE`](LICENSE).
