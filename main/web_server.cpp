#include "web_server.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "esp_app_desc.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"

#include "app_config.hpp"
#include "matrix_display.hpp"
#include "tiny_font.hpp"
#include "clock_theme.hpp"
#include "timezone_utils.hpp"
#include "sensor_service.hpp"
#include "weather_service.hpp"
#include "wifi_manager.hpp"

static const char* TAG = "web";
static ClockConfig* s_config = nullptr;
static httpd_handle_t s_server = nullptr;
static volatile bool s_ota_in_progress = false;
static MatrixDisplay* s_display = nullptr;

namespace {
std::string html_escape(const char* src) {
    std::string out;
    if (!src) return out;
    while (*src) {
        switch (*src) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out += *src; break;
        }
        ++src;
    }
    return out;
}

std::string json_escape(const char* src) {
    std::string out;
    if (!src) return out;
    for (; *src; ++src) {
        if (*src == '"' || *src == '\\') out += '\\';
        if (static_cast<unsigned char>(*src) >= 0x20) out += *src;
    }
    return out;
}

int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + c - 'a';
    if (c >= 'A' && c <= 'F') return 10 + c - 'A';
    return -1;
}

std::string url_decode(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '+') out += ' ';
        else if (in[i] == '%' && i + 2 < in.size()) {
            int h = hexval(in[i + 1]);
            int l = hexval(in[i + 2]);
            if (h >= 0 && l >= 0) {
                out += static_cast<char>((h << 4) | l);
                i += 2;
            } else out += in[i];
        } else out += in[i];
    }
    return out;
}

std::string form_value(const std::string& body, const char* key) {
    std::string prefix = std::string(key) + "=";
    size_t pos = 0;
    while (pos <= body.size()) {
        size_t end = body.find('&', pos);
        if (end == std::string::npos) end = body.size();
        const std::string item = body.substr(pos, end - pos);
        if (item.rfind(prefix, 0) == 0) return url_decode(item.substr(prefix.size()));
        if (end == body.size()) break;
        pos = end + 1;
    }
    return "";
}

bool form_has_key(const std::string& body, const char* key) {
    const std::string plain = std::string(key);
    const std::string with_equal = plain + "=";
    if (body == plain || body.rfind(with_equal, 0) == 0) return true;
    if (body.find("&" + plain + "&") != std::string::npos) return true;
    if (body.size() >= plain.size() + 1 && body.compare(body.size() - plain.size() - 1, plain.size() + 1, "&" + plain) == 0) return true;
    return body.find("&" + with_equal) != std::string::npos;
}

void safe_copy(char* dst, size_t dst_size, const std::string& value) {
    if (dst_size == 0) return;
    const size_t n = std::min(dst_size - 1, value.size());
    std::memcpy(dst, value.data(), n);
    dst[n] = '\0';
}

bool authenticate(httpd_req_t* req) {
    if (!s_config) return false;

    size_t len = httpd_req_get_hdr_value_len(req, "Authorization");
    if (len == 0 || len > 255) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"myClock\"");
        httpd_resp_send(req, "Login required", HTTPD_RESP_USE_STRLEN);
        return false;
    }

    char auth[256]{};
    if (httpd_req_get_hdr_value_str(req, "Authorization", auth, sizeof(auth)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid Authorization header");
        return false;
    }

    std::string plain = std::string("admin:") + s_config->admin_password;
    unsigned char encoded[192]{};
    size_t olen = 0;
    if (mbedtls_base64_encode(encoded, sizeof(encoded), &olen,
                              reinterpret_cast<const unsigned char*>(plain.data()), plain.size()) != 0) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Authentication error");
        return false;
    }
    std::string expected = "Basic " + std::string(reinterpret_cast<char*>(encoded), olen);
    if (expected != auth) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"myClock\"");
        httpd_resp_send(req, "Wrong username/password", HTTPD_RESP_USE_STRLEN);
        return false;
    }
    return true;
}

std::string receive_body(httpd_req_t* req, size_t max_len) {
    if (req->content_len <= 0 || static_cast<size_t>(req->content_len) > max_len) return "";
    std::string body(static_cast<size_t>(req->content_len), '\0');
    size_t received = 0;
    while (received < body.size()) {
        int r = httpd_req_recv(req, body.data() + received, body.size() - received);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (r <= 0) return "";
        received += static_cast<size_t>(r);
    }
    return body;
}

void schedule_restart() {
    esp_timer_handle_t timer{};
    esp_timer_create_args_t args{};
    args.callback = [](void*) { esp_restart(); };
    args.name = "restart";
    if (esp_timer_create(&args, &timer) == ESP_OK) esp_timer_start_once(timer, 1000 * 1000);
}


std::string clock_theme_options_html(uint8_t selected) {
    std::size_t count = 0;
    const ClockThemeChoice* themes = clock_theme_choices(count);
    std::string out;
    for (std::size_t i = 0; i < count; ++i) {
        char rgb[16];
        std::snprintf(rgb, sizeof(rgb), "#%02X%02X%02X", themes[i].r, themes[i].g, themes[i].b);
        out += "<option value='" + std::to_string(themes[i].id) + "' data-color='" + std::string(rgb) + "'";
        if (themes[i].id == selected) out += " selected";
        out += ">" + html_escape(themes[i].label) + "</option>";
    }
    out += "<option value='255'";
    if (selected == 255) out += " selected";
    out += ">Benutzerdefiniert</option>";
    return out;
}

bool parse_html_color(const std::string& value, uint8_t& r, uint8_t& g, uint8_t& b) {
    if (value.size() != 7 || value[0] != '#') return false;
    int n[6];
    for (int i = 0; i < 6; ++i) {
        n[i] = hexval(value[static_cast<size_t>(i) + 1]);
        if (n[i] < 0) return false;
    }
    r = static_cast<uint8_t>((n[0] << 4) | n[1]);
    g = static_cast<uint8_t>((n[2] << 4) | n[3]);
    b = static_cast<uint8_t>((n[4] << 4) | n[5]);
    return true;
}

std::string timezone_options_html(const char* selected) {
    size_t count = 0;
    const TimezoneChoice* choices = timezone_choices(count);
    std::string out;
    for (size_t i = 0; i < count; ++i) {
        out += "<option value='" + html_escape(choices[i].iana) + "'";
        if (selected && std::strcmp(selected, choices[i].iana) == 0) out += " selected";
        out += ">" + html_escape(choices[i].label) + "</option>";
    }
    return out;
}

esp_err_t root_handler(httpd_req_t* req) {
    if (!authenticate(req)) return ESP_OK;

    WifiStatus ws{};
    wifi_manager_get_status(ws);
    WeatherStatus weather{};
    weather_service_get_status(weather);
    SensorStatus sensors{};
    sensor_service_get_status(sensors);
    const esp_app_desc_t* app = esp_app_get_description();

    std::string html = R"HTML(<!doctype html><html lang="de"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>myClock</title>
<style>
body{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;background:#111;color:#eee;margin:0;padding:18px}
main{max-width:760px;margin:auto}.card{background:#1d1d1f;border-radius:14px;padding:18px;margin:0 0 16px}
h1{margin-top:0}h2{font-size:1.1rem}.grid{display:grid;grid-template-columns:190px 1fr;gap:8px 12px}
label{display:block;margin:12px 0 4px}input,select{width:100%;box-sizing:border-box;padding:9px;border-radius:8px;border:1px solid #555;background:#29292c;color:#fff}
.inline{display:flex;gap:10px;align-items:center}.inline input[type=checkbox]{width:auto}.inline label{margin:0}
button{padding:10px 15px;border:0;border-radius:9px;font-weight:600;cursor:pointer;margin:10px 6px 0 0}.primary{background:#4d8dff;color:#fff}.danger{background:#c63838;color:#fff}.secondary{background:#555;color:#fff}.muted{color:#aaa;font-size:.9rem}.ok{color:#69d17d}.bad{color:#ff7777}
progress{width:100%;height:18px}code{color:#b6d7ff}.swatch{display:inline-block;width:18px;height:18px;border-radius:50%;border:1px solid #777;vertical-align:middle;margin-left:8px}input[type=color]{height:46px;padding:4px;cursor:pointer}.tablewrap{overflow-x:auto;margin-top:10px}table{width:100%;border-collapse:collapse;font-size:.9rem}th,td{text-align:left;padding:8px 7px;border-bottom:1px solid #3a3a3c;white-space:nowrap}th{color:#aaa}.bestap{color:#69d17d;font-weight:700}.currentap{color:#b6d7ff;font-weight:700}.badge{display:inline-block;padding:2px 6px;margin-left:4px;border-radius:10px;background:#333;font-size:.75rem}@media(max-width:600px){.grid{grid-template-columns:1fr}.inline{align-items:flex-start}}
</style></head><body><main><h1>myClock</h1>)HTML";

    html += "<div class='card'><h2>Status</h2><div class='grid'>";
    html += "<div>Hostname</div><div>" + html_escape(ws.hostname) + "</div>";
    html += "<div>WLAN</div><div class='" + std::string(ws.sta_connected ? "ok" : "bad") + "'>" +
            std::string(ws.sta_connected ? "verbunden" : "nicht verbunden") + "</div>";
    html += "<div>SSID</div><div>" + html_escape(ws.ssid) + "</div>";
    html += "<div>IP</div><div>" + html_escape(ws.ip) + "</div>";
    html += "<div>Access Point / BSSID</div><div>" + html_escape(ws.bssid) + "</div>";
    html += "<div>RSSI</div><div>" + std::to_string(ws.rssi) + " dBm</div>";
    html += "<div>Kanal</div><div>" + std::to_string(ws.channel) + "</div>";
    html += "<div>AP-Auswahl</div><div class='ok'>automatisch: staerkster AP mit gleicher SSID</div>";
    if (ws.fallback_ap) html += "<div>Fallback-AP</div><div class='ok'>" + html_escape(ws.ap_ssid) + " / " + html_escape(ws.ap_ip) + "</div>";
    html += "<div>Zeitzone</div><div>" + html_escape(s_config->timezone_name) + "</div>";
    html += "<div>POSIX TZ</div><div><code>" + html_escape(s_config->timezone) + "</code></div>";
    char theme_rgb[16];
    std::snprintf(theme_rgb, sizeof(theme_rgb), "#%02X%02X%02X", s_config->clock_r, s_config->clock_g, s_config->clock_b);
    std::string theme_label = "Benutzerdefiniert";
    if (s_config->clock_theme <= 9) theme_label = clock_theme_by_id(s_config->clock_theme).label;
    html += "<div>Uhrfarbe</div><div>" + html_escape(theme_label.c_str()) + " " + std::string(theme_rgb) + " <span class='swatch' style='background:" + std::string(theme_rgb) + "'></span></div>";
    html += "<div>Wetter</div><div class='" + std::string(weather.valid ? "ok" : (weather.enabled ? "bad" : "")) + "'>";
    if (!weather.enabled) html += "deaktiviert";
    else if (weather.valid) {
        char w[160];
        std::snprintf(w, sizeof(w), "%.1f %c, %d%%, %.1f %s, %s",
                      weather.temperature, s_config->weather_celsius ? 'C' : 'F', weather.humidity,
                      weather.wind_speed, weather.wind_dir, weather.description);
        html += html_escape(w);
    } else html += weather.error[0] ? html_escape(weather.error) : "noch keine Daten";
    html += "</div>";

    html += "<div>DS18B20 (GPIO17)</div><div class='" + std::string(sensors.ds18_valid ? "ok" : (sensors.ds18_enabled ? "bad" : "")) + "'>";
    if (!sensors.ds18_enabled) html += "deaktiviert";
    else if (sensors.ds18_valid) {
        float local_temp = sensors.ds18_temperature_c;
        if (!s_config->weather_celsius) local_temp = local_temp * 1.8f + 32.0f;
        local_temp += static_cast<float>(s_config->ds18_temp_offset);
        char t[64];
        std::snprintf(t, sizeof(t), "%.1f %c (Offset %+d)", local_temp,
                      s_config->weather_celsius ? 'C' : 'F', s_config->ds18_temp_offset);
        html += html_escape(t);
    } else html += sensors.ds18_error[0] ? html_escape(sensors.ds18_error) : "noch kein Messwert";
    html += "</div>";

    html += "<div>LDR (A0/GPIO36)</div><div class='" + std::string(sensors.ldr_valid ? "ok" : (sensors.ldr_enabled ? "bad" : "")) + "'>";
    if (!sensors.ldr_enabled) html += "deaktiviert";
    else if (sensors.ldr_valid) {
        char l[96];
        std::snprintf(l, sizeof(l), "raw %d, gefiltert %d, Helligkeit %u/255", sensors.ldr_raw,
                      sensors.ldr_filtered, sensors.effective_brightness);
        html += html_escape(l);
    } else html += sensors.ldr_error[0] ? html_escape(sensors.ldr_error) : "noch kein Messwert";
    html += "</div>";

    html += "<div>Firmware</div><div>" + html_escape(app ? app->version : "?") + "</div>";
    html += "<div>Build</div><div>" + std::string(__DATE__) + " " + std::string(__TIME__) + "</div>";
    html += "<div>ESP-IDF</div><div>" + html_escape(esp_get_idf_version()) + "</div></div></div>";

    html += R"HTML(<div class='card'><h2>WLAN / Access Points</h2>
<p class='muted'>Scannt die Umgebung und zeigt jede BSSID separat. Fuer die konfigurierte SSID wird der Access Point mit dem staerksten Signal markiert. Beim Start und nach einem Verbindungsabbruch scannt myClock die SSID und verbindet sich gezielt mit der staerksten gefundenen BSSID. Während eines manuellen Scans kann die WLAN-Verbindung fuer einige Sekunden kurz stocken.</p>
<button class='secondary' id='wifiScanBtn' type='button' onclick='scanWifi()'>WLAN scannen</button>
<div id='wifiScanSummary' class='muted' style='margin-top:10px'>Noch kein Scan ausgefuehrt.</div>
<div class='tablewrap'><table id='wifiScanTable' style='display:none'><thead><tr><th>SSID</th><th>BSSID</th><th>Kanal</th><th>RSSI</th><th>Status</th></tr></thead><tbody></tbody></table></div>
<script>
function rssiText(r){if(r>=-50)return 'sehr stark';if(r>=-60)return 'stark';if(r>=-70)return 'gut';if(r>=-80)return 'schwach';return 'sehr schwach';}
function esc(s){return String(s??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));}
async function scanWifi(){
 const b=document.getElementById('wifiScanBtn'),sum=document.getElementById('wifiScanSummary'),tab=document.getElementById('wifiScanTable'),body=tab.querySelector('tbody');
 b.disabled=true;b.textContent='Scan laeuft...';sum.textContent='Suche Access Points...';body.innerHTML='';tab.style.display='none';
 try{
  const res=await fetch('/wifi/scan',{cache:'no-store'});const d=await res.json();if(!res.ok||!d.ok)throw new Error(d.error||('HTTP '+res.status));
  const own=d.results.filter(x=>x.configured);const best=own.length?own[0]:null;const current=d.results.find(x=>x.current)||null;
  if(best){let txt='Bester AP fuer '+d.configured_ssid+': '+best.bssid+' / '+best.rssi+' dBm / Kanal '+best.channel;
   if(current&&current.bssid!==best.bssid)txt+=' — aktuell '+current.bssid+' / '+current.rssi+' dBm ('+(best.rssi-current.rssi)+' dB schlechter als bester AP).';
   else if(current)txt+=' — dieser AP ist aktuell verbunden.';sum.textContent=txt;
  }else if(d.configured_ssid){sum.textContent='Konfigurierte SSID '+d.configured_ssid+' wurde beim Scan nicht gefunden.';}
  else{sum.textContent='Keine WLAN-SSID konfiguriert. '+d.results.length+' Access Points gefunden.';}
  for(const x of d.results){const tr=document.createElement('tr');if(x.best)tr.className='bestap';else if(x.current)tr.className='currentap';
   let st='';if(x.best)st+='<span class="badge">bester AP</span>';if(x.current)st+='<span class="badge">aktuell</span>';if(x.configured&&!x.best&&!x.current)st+='<span class="badge">gleiche SSID</span>';
   tr.innerHTML='<td>'+esc(x.ssid||'(versteckt)')+'</td><td><code>'+esc(x.bssid)+'</code></td><td>'+x.channel+'</td><td>'+x.rssi+' dBm<br><span class="muted">'+rssiText(x.rssi)+'</span></td><td>'+st+'</td>';body.appendChild(tr);}
  tab.style.display=d.results.length?'table':'none';if(!d.results.length)sum.textContent='Keine Access Points gefunden.';
 }catch(e){sum.textContent='WLAN-Scan fehlgeschlagen: '+e.message;}
 finally{b.disabled=false;b.textContent='WLAN scannen';}
}
</script></div>)HTML";

    html += "<div class='card'><h2>Konfiguration</h2><form method='post' action='/save' id='cfgForm'>";
    html += "<label>WLAN SSID</label><input name='ssid' maxlength='32' value='" + html_escape(s_config->wifi_ssid) + "'>";
    html += "<label>WLAN Passwort</label><input name='wifi_password' type='password' maxlength='64' placeholder='leer = bisheriges Passwort behalten'>";
    html += "<label>Web/Admin Passwort</label><input name='admin_password' type='password' maxlength='64' placeholder='leer = bisheriges Passwort behalten'>";
    html += "<div class='muted'>Benutzername ist <b>admin</b>. Das Admin-Passwort wird auch fuer den Fallback-AP verwendet; fuer WPA2 mindestens 8 Zeichen.</div>";
    html += "<label>Helligkeit (1-255)</label><input name='brightness' type='number' min='1' max='255' value='" + std::to_string(s_config->brightness) + "'>";
    html += "<label>Farb-Preset</label><select id='clockTheme'>" + clock_theme_options_html(s_config->clock_theme) + "</select>";
    html += "<label>Farbe der Uhrzeit</label><input name='clock_color' id='clockColor' type='color' value='" + std::string(theme_rgb) + "'>";
    html += "<label>Farbcode (#RRGGBB)</label><input id='clockColorText' type='text' inputmode='text' maxlength='7' pattern='#[0-9A-Fa-f]{6}' value='" + std::string(theme_rgb) + "' placeholder='#FF4C04' autocomplete='off' spellcheck='false'>";
    html += "<div class='muted'>Farbpicker und Farbcode sind gekoppelt. Du kannst z.B. <code>#FF4C04</code> direkt eingeben. Presets setzen beide Felder. Aktuell: <code id='clockColorHex'>" + std::string(theme_rgb) + "</code> <span id='themePreview' class='swatch' style='background:" + std::string(theme_rgb) + "'></span><br>Gilt nur fuer die grossen Uhrzeit-Ziffern und Doppelpunkte. Durch die 8-stufige Farbansteuerung der Matrix kann die reale LED-Farbe etwas vom Browser abweichen.</div>";
    html += "<label>Zeitanzeige</label><select name='h24'><option value='1'" + std::string(s_config->use_24h ? " selected" : "") + ">24 Stunden</option><option value='0'" + std::string(!s_config->use_24h ? " selected" : "") + ">12 Stunden</option></select>";

    html += "<label>Zeitzone</label><select name='tz_mode' id='tzMode'><option value='auto'" + std::string(s_config->timezone_auto ? " selected" : "") + ">Automatisch vom Browser</option><option value='manual'" + std::string(!s_config->timezone_auto ? " selected" : "") + ">Manuell auswaehlen</option></select>";
    html += "<input type='hidden' name='browser_tz' id='browserTz' value=''>";
    html += "<div class='muted'>Vom Browser erkannt: <b id='detectedTz'>wird ermittelt...</b></div>";
    html += "<label>Manuelle Zeitzone</label><select name='tz_name' id='tzName'>" + timezone_options_html(s_config->timezone_name) + "</select>";
    html += "<div class='muted'>Intern verwendet der ESP32 weiterhin einen POSIX-TZ-String; du musst ihn nicht selbst eingeben.</div>";

    html += "<hr style='border:0;border-top:1px solid #444;margin:20px 0'><h2>Wetter (OpenWeather)</h2>";
    html += "<div class='inline'><input name='weather_enabled' id='weatherEnabled' type='checkbox' value='1'" + std::string(s_config->weather_enabled ? " checked" : "") + "><label for='weatherEnabled'>Wetteranzeige aktivieren</label></div>";
    html += "<label>OpenWeather API-Key</label><input name='openweather_key' type='password' maxlength='64' placeholder='" + std::string(s_config->openweather_key[0] ? "gesetzt - leer = bisherigen Key behalten" : "API-Key eintragen") + "'>";
    html += "<label>Postleitzahl</label><input name='weather_zip' maxlength='16' value='" + html_escape(s_config->weather_zip) + "' placeholder='z.B. 12345'>";
    html += "<label>Land (2-stelliger Code)</label><input name='weather_country' maxlength='2' value='" + html_escape(s_config->weather_country) + "' placeholder='DE'>";
    html += "<label>Wetter-Sprache</label><select name='weather_language'><option value='en'" + std::string(std::strcmp(s_config->weather_language, "en") == 0 ? " selected" : "") + ">Englisch</option><option value='de'" + std::string(std::strcmp(s_config->weather_language, "de") == 0 ? " selected" : "") + ">Deutsch</option></select>";
    html += "<label>Einheiten</label><select name='weather_celsius'><option value='1'" + std::string(s_config->weather_celsius ? " selected" : "") + ">Celsius / metrisch</option><option value='0'" + std::string(!s_config->weather_celsius ? " selected" : "") + ">Fahrenheit / imperial</option></select>";
    html += "<label>Aktualisierung (Minuten)</label><input name='weather_interval' type='number' min='5' max='180' value='" + std::to_string(s_config->weather_interval_min) + "'>";

    html += "<hr style='border:0;border-top:1px solid #444;margin:20px 0'><h2>Sensoren</h2>";
    html += "<div class='muted'>GPIOs bleiben wie in der Original-Uhr: DS18B20 auf GPIO17, LDR auf A0/GPIO36.</div>";
    html += "<div class='inline'><input name='ds18_enabled' id='ds18Enabled' type='checkbox' value='1'" + std::string(s_config->ds18_enabled ? " checked" : "") + "><label for='ds18Enabled'>DS18B20 Innentemperatur aktivieren</label></div>";
    html += "<label>Temperatur-Offset</label><input name='ds18_offset' type='number' min='-20' max='20' step='1' value='" + std::to_string(s_config->ds18_temp_offset) + "'>";
    html += "<div class='muted'>Offset in der aktuell ausgewaehlten Temperatureinheit (C/F), wie im Original.</div>";
    html += "<div class='inline' style='margin-top:14px'><input name='ldr_enabled' id='ldrEnabled' type='checkbox' value='1'" + std::string(s_config->ldr_enabled ? " checked" : "") + "><label for='ldrEnabled'>LDR automatische Helligkeit aktivieren</label></div>";
    html += "<label>LDR Schwellwert (0-4095)</label><input name='ldr_threshold' type='number' min='1' max='4095' value='" + std::to_string(s_config->ldr_threshold) + "'>";
    html += "<div class='muted'>Originalwert: 1000. Oberhalb davon volle konfigurierte Helligkeit; darunter stufenweise 1/2, 1/4, 1/8 und 1/16.</div>";

    html += "<button class='primary' type='submit'>Speichern & Neustarten</button></form>";
    html += R"HTML(<script>
(function(){
 let tz='';try{tz=Intl.DateTimeFormat().resolvedOptions().timeZone||'';}catch(e){}
 document.getElementById('browserTz').value=tz;document.getElementById('detectedTz').textContent=tz||'nicht verfuegbar';
 const sel=document.getElementById('clockTheme'), picker=document.getElementById('clockColor'), text=document.getElementById('clockColorText'), preview=document.getElementById('themePreview'), hex=document.getElementById('clockColorHex'), form=document.getElementById('cfgForm');
 function norm(v){return (v||'').toUpperCase();}
 function validHex(v){return /^#[0-9A-F]{6}$/.test(norm(v).trim());}
 function syncPreset(){let match='255';for(const o of sel.options){if(o.dataset.color&&norm(o.dataset.color)===norm(picker.value)){match=o.value;break;}}sel.value=match;}
 function updateColorUI(syncText=true){const c=norm(picker.value);preview.style.background=c;hex.textContent=c;if(syncText)text.value=c;syncPreset();}
 sel.addEventListener('change',()=>{const o=sel.options[sel.selectedIndex];if(o&&o.dataset.color){picker.value=o.dataset.color.toLowerCase();updateColorUI(true);}});
 picker.addEventListener('input',()=>updateColorUI(true));
 text.addEventListener('input',()=>{const c=norm(text.value).trim();text.value=c;if(validHex(c)){picker.value=c.toLowerCase();updateColorUI(false);text.setCustomValidity('');}else{text.setCustomValidity('Bitte Farbcode im Format #RRGGBB eingeben, z.B. #FF4C04');}});
 text.addEventListener('blur',()=>{if(validHex(text.value)){text.value=norm(text.value).trim();text.setCustomValidity('');}});
 form.addEventListener('submit',e=>{if(!validHex(text.value)){text.setCustomValidity('Bitte Farbcode im Format #RRGGBB eingeben, z.B. #FF4C04');text.reportValidity();e.preventDefault();return;}picker.value=norm(text.value).trim().toLowerCase();});
 updateColorUI(true);
})();
</script>)HTML";
    html += "</div>";

    html += R"HTML(<div class='card'><h2>Wetter aktualisieren</h2>
<p class='muted'>Startet sofort eine neue OpenWeather-Abfrage, ohne die Uhr neu zu starten.</p>
<form method='post' action='/weather/update'><button class='secondary' type='submit'>Jetzt aktualisieren</button></form></div>
<div class='card'><h2>OTA Firmware</h2>
<p class='muted'>Die <code>build/myClock_idf.bin</code> auswaehlen. Nach erfolgreichem Upload startet die Uhr neu.</p>
<input id='fw' type='file' accept='.bin,application/octet-stream'><button class='primary' type='button' onclick='uploadFw()'>Firmware hochladen</button>
<progress id='prog' value='0' max='100' style='display:none'></progress><div id='otaMsg' class='muted'></div>
<script>
function uploadFw(){const f=document.getElementById('fw').files[0],p=document.getElementById('prog'),m=document.getElementById('otaMsg');if(!f){m.textContent='Bitte zuerst eine .bin Datei auswaehlen.';return;}p.style.display='block';p.value=0;m.textContent='Upload laeuft...';const x=new XMLHttpRequest();x.open('POST','/ota');x.setRequestHeader('Content-Type','application/octet-stream');x.upload.onprogress=e=>{if(e.lengthComputable)p.value=Math.round(e.loaded*100/e.total);};x.onload=()=>{p.value=100;m.textContent=x.responseText;if(x.status>=200&&x.status<300)setTimeout(()=>location.reload(),7000);};x.onerror=()=>{m.textContent='Fehler beim Upload';p.value=0;};x.send(f);}
</script></div>
<div class='card'><h2>System</h2><form method='post' action='/reboot' onsubmit="return confirm('Uhr wirklich neu starten?')"><button class='danger' type='submit'>Neustarten</button></form></div>
</main></body></html>)HTML";

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, html.c_str(), html.size());
    return ESP_OK;
}

esp_err_t wifi_scan_handler(httpd_req_t* req) {
    if (!authenticate(req)) return ESP_OK;

    WifiScanResult results[32]{};
    const int count = wifi_manager_scan(results, 32);
    if (count < 0) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"Scan konnte nicht ausgefuehrt werden\"}");
        return ESP_OK;
    }

    WifiStatus ws{};
    wifi_manager_get_status(ws);
    const char* configured = s_config ? s_config->wifi_ssid : "";

    int best_index = -1;
    for (int i = 0; i < count; ++i) {
        if (configured[0] && std::strcmp(results[i].ssid, configured) == 0) {
            best_index = i;
            break;  // results are sorted strongest first
        }
    }

    std::string json = "{\"ok\":true,\"configured_ssid\":\"" + json_escape(configured) +
                       "\",\"current_bssid\":\"" + json_escape(ws.bssid) + "\",\"results\":[";
    for (int i = 0; i < count; ++i) {
        if (i) json += ',';
        const bool configured_match = configured[0] && std::strcmp(results[i].ssid, configured) == 0;
        const bool current = ws.bssid[0] && std::strcmp(results[i].bssid, ws.bssid) == 0;
        const bool best = i == best_index;
        json += "{\"ssid\":\"" + json_escape(results[i].ssid) +
                "\",\"bssid\":\"" + json_escape(results[i].bssid) +
                "\",\"rssi\":" + std::to_string(results[i].rssi) +
                ",\"channel\":" + std::to_string(results[i].channel) +
                ",\"configured\":" + (configured_match ? "true" : "false") +
                ",\"current\":" + (current ? "true" : "false") +
                ",\"best\":" + (best ? "true" : "false") + "}";
    }
    json += "]}";

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_send(req, json.c_str(), json.size());
    return ESP_OK;
}

esp_err_t status_handler(httpd_req_t* req) {
    if (!authenticate(req)) return ESP_OK;
    WifiStatus ws{};
    wifi_manager_get_status(ws);
    WeatherStatus weather{};
    weather_service_get_status(weather);
    SensorStatus sensors{};
    sensor_service_get_status(sensors);
    const esp_app_desc_t* app = esp_app_get_description();
    char json[2304]{};
    float local_temp = sensors.ds18_temperature_c;
    if (!s_config->weather_celsius) local_temp = local_temp * 1.8f + 32.0f;
    local_temp += static_cast<float>(s_config->ds18_temp_offset);
    std::snprintf(json, sizeof(json),
        "{\"hostname\":\"%s\",\"connected\":%s,\"ssid\":\"%s\",\"ip\":\"%s\","
        "\"bssid\":\"%s\",\"rssi\":%d,\"channel\":%d,\"fallback_ap\":%s,"
        "\"ap_ssid\":\"%s\",\"ap_ip\":\"%s\",\"version\":\"%s\","
        "\"timezone_name\":\"%s\",\"clock_theme\":%u,\"clock_theme_name\":\"%s\",\"clock_color\":\"#%02X%02X%02X\",\"weather_enabled\":%s,\"weather_valid\":%s,"
        "\"weather_temp\":%.2f,\"weather_humidity\":%d,\"weather_description\":\"%s\",\"weather_error\":\"%s\","
        "\"ds18_enabled\":%s,\"ds18_found\":%s,\"ds18_valid\":%s,\"ds18_temp\":%.2f,\"ds18_error\":\"%s\","
        "\"ldr_enabled\":%s,\"ldr_valid\":%s,\"ldr_raw\":%d,\"ldr_filtered\":%d,\"effective_brightness\":%u,\"ldr_error\":\"%s\"}",
        json_escape(ws.hostname).c_str(), ws.sta_connected ? "true" : "false",
        json_escape(ws.ssid).c_str(), json_escape(ws.ip).c_str(), json_escape(ws.bssid).c_str(),
        ws.rssi, ws.channel, ws.fallback_ap ? "true" : "false",
        json_escape(ws.ap_ssid).c_str(), json_escape(ws.ap_ip).c_str(),
        app ? json_escape(app->version).c_str() : "",
        json_escape(s_config->timezone_name).c_str(), s_config->clock_theme,
        json_escape(s_config->clock_theme <= 9 ? clock_theme_by_id(s_config->clock_theme).label : "Benutzerdefiniert").c_str(),
        s_config->clock_r, s_config->clock_g, s_config->clock_b, weather.enabled ? "true" : "false",
        weather.valid ? "true" : "false", weather.temperature, weather.humidity,
        json_escape(weather.description).c_str(), json_escape(weather.error).c_str(),
        sensors.ds18_enabled ? "true" : "false", sensors.ds18_found ? "true" : "false",
        sensors.ds18_valid ? "true" : "false", local_temp, json_escape(sensors.ds18_error).c_str(),
        sensors.ldr_enabled ? "true" : "false", sensors.ldr_valid ? "true" : "false",
        sensors.ldr_raw, sensors.ldr_filtered, sensors.effective_brightness, json_escape(sensors.ldr_error).c_str());
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    return ESP_OK;
}

esp_err_t save_handler(httpd_req_t* req) {
    if (!authenticate(req)) return ESP_OK;
    std::string body = receive_body(req, 8192);
    if (body.empty()) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Leere oder zu grosse Anfrage");
        return ESP_OK;
    }

    ClockConfig next = *s_config;
    const std::string ssid = form_value(body, "ssid");
    const bool ssid_changed = ssid != next.wifi_ssid;
    safe_copy(next.wifi_ssid, sizeof(next.wifi_ssid), ssid);

    const std::string wifi_pass = form_value(body, "wifi_password");
    if (!wifi_pass.empty()) safe_copy(next.wifi_password, sizeof(next.wifi_password), wifi_pass);
    else if (ssid_changed) next.wifi_password[0] = '\0';

    const std::string admin_pass = form_value(body, "admin_password");
    if (!admin_pass.empty()) {
        if (admin_pass.size() < 8) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Admin-Passwort muss mindestens 8 Zeichen haben");
            return ESP_OK;
        }
        safe_copy(next.admin_password, sizeof(next.admin_password), admin_pass);
    }

    const std::string bright = form_value(body, "brightness");
    int b = bright.empty() ? next.brightness : std::atoi(bright.c_str());
    if (b < 1 || b > 255) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Helligkeit muss 1..255 sein");
        return ESP_OK;
    }
    next.brightness = static_cast<uint8_t>(b);

    const std::string clock_color = form_value(body, "clock_color");
    uint8_t clock_r = next.clock_r, clock_g = next.clock_g, clock_b = next.clock_b;
    if (!parse_html_color(clock_color, clock_r, clock_g, clock_b)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Ungueltige Uhrfarbe; erwartet wird #RRGGBB");
        return ESP_OK;
    }
    next.clock_r = clock_r;
    next.clock_g = clock_g;
    next.clock_b = clock_b;
    next.clock_theme = clock_theme_id_for_rgb(clock_r, clock_g, clock_b);
    next.use_24h = form_value(body, "h24") != "0";

    const std::string tz_mode = form_value(body, "tz_mode");
    const std::string requested_tz = tz_mode == "auto" ? form_value(body, "browser_tz") : form_value(body, "tz_name");
    if (requested_tz.empty()) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Keine Zeitzone ermittelt/ausgewaehlt");
        return ESP_OK;
    }
    char posix[sizeof(next.timezone)]{};
    if (!timezone_iana_to_posix(requested_tz.c_str(), posix, sizeof(posix))) {
        std::string msg = "Zeitzone '" + requested_tz + "' ist in dieser Firmware noch nicht hinterlegt. Bitte manuell eine unterstuetzte Zeitzone waehlen.";
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, msg.c_str());
        return ESP_OK;
    }
    safe_copy(next.timezone_name, sizeof(next.timezone_name), requested_tz);
    safe_copy(next.timezone, sizeof(next.timezone), posix);
    next.timezone_auto = tz_mode == "auto";

    next.weather_enabled = form_has_key(body, "weather_enabled");
    const std::string ow_key = form_value(body, "openweather_key");
    if (!ow_key.empty()) safe_copy(next.openweather_key, sizeof(next.openweather_key), ow_key);
    safe_copy(next.weather_zip, sizeof(next.weather_zip), form_value(body, "weather_zip"));
    std::string country = form_value(body, "weather_country");
    std::transform(country.begin(), country.end(), country.begin(), [](unsigned char c){ return static_cast<char>(std::toupper(c)); });
    safe_copy(next.weather_country, sizeof(next.weather_country), country);
    safe_copy(next.weather_language, sizeof(next.weather_language), form_value(body, "weather_language"));
    next.weather_celsius = form_value(body, "weather_celsius") != "0";
    const int interval = std::atoi(form_value(body, "weather_interval").c_str());
    if (interval < 5 || interval > 180) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Wetterintervall muss 5..180 Minuten sein");
        return ESP_OK;
    }
    next.weather_interval_min = static_cast<uint16_t>(interval);

    if (next.weather_enabled && (!next.openweather_key[0] || !next.weather_zip[0] || std::strlen(next.weather_country) != 2)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Fuer Wetter bitte API-Key, PLZ und 2-stelligen Laendercode eintragen");
        return ESP_OK;
    }

    next.ds18_enabled = form_has_key(body, "ds18_enabled");
    const int ds18_offset = std::atoi(form_value(body, "ds18_offset").c_str());
    if (ds18_offset < -20 || ds18_offset > 20) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Temperatur-Offset muss -20..20 sein");
        return ESP_OK;
    }
    next.ds18_temp_offset = static_cast<int8_t>(ds18_offset);

    next.ldr_enabled = form_has_key(body, "ldr_enabled");
    const int ldr_threshold = std::atoi(form_value(body, "ldr_threshold").c_str());
    if (ldr_threshold < 1 || ldr_threshold > 4095) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "LDR-Schwellwert muss 1..4095 sein");
        return ESP_OK;
    }
    next.ldr_threshold = static_cast<uint16_t>(ldr_threshold);

    if (!config_save(next)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Konfiguration konnte nicht gespeichert werden");
        return ESP_OK;
    }
    *s_config = next;
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_sendstr(req, "<html><body><h2>Gespeichert.</h2><p>myClock startet neu...</p></body></html>");
    schedule_restart();
    return ESP_OK;
}

esp_err_t weather_update_handler(httpd_req_t* req) {
    if (!authenticate(req)) return ESP_OK;
    weather_service_request_update();
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_sendstr(req, "<html><body><h2>Wetterabfrage gestartet.</h2><p><a href='/'>Zurueck</a></p></body></html>");
    return ESP_OK;
}

esp_err_t reboot_handler(httpd_req_t* req) {
    if (!authenticate(req)) return ESP_OK;
    httpd_resp_sendstr(req, "myClock startet neu...");
    schedule_restart();
    return ESP_OK;
}

esp_err_t ota_handler(httpd_req_t* req) {
    if (!authenticate(req)) return ESP_OK;
    if (req->content_len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Keine Firmware empfangen");
        return ESP_OK;
    }

    const esp_partition_t* update = esp_ota_get_next_update_partition(nullptr);
    if (!update) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Keine OTA-Partition gefunden");
        return ESP_OK;
    }
    if (static_cast<size_t>(req->content_len) > update->size) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Firmware ist groesser als die OTA-Partition");
        return ESP_OK;
    }

    // First show a short, deterministic OTA notice, then blank the panel before
    // any flash erase/write starts. This gives visible feedback without keeping
    // the HUB75 refresh active while flash operations can stall tasks.
    s_ota_in_progress = true;
    if (s_display) {
        // Give app_main enough time to observe the OTA flag and stop all normal
        // clock/weather framebuffer updates before we draw the notice directly.
        vTaskDelay(pdMS_TO_TICKS(75));

        s_display->clear();
        const uint16_t ota_color = s_config
            ? MatrixDisplay::color565(s_config->clock_r, s_config->clock_g, s_config->clock_b)
            : MatrixDisplay::color565(32, 64, 160);
        tiny_draw_text_centered(*s_display, 13, "Updating", ota_color);
        s_display->setOutputEnabled(true);
        ESP_LOGI(TAG, "OTA display notice: Updating");

        // Keep the notice visible briefly so it is actually perceptible, then
        // hard-disable HUB75 output before esp_ota_begin()/esp_ota_write().
        vTaskDelay(pdMS_TO_TICKS(800));
        s_display->setOutputEnabled(false);
        ESP_LOGI(TAG, "OTA display blanked (OE HIGH)");
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    ESP_LOGI(TAG, "OTA upload: %d bytes -> %s", req->content_len, update->label);
    esp_ota_handle_t handle{};
    esp_err_t err = esp_ota_begin(update, static_cast<size_t>(req->content_len), &handle);
    if (err != ESP_OK) {
        if (s_display) s_display->setOutputEnabled(true);
        s_ota_in_progress = false;
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA konnte nicht gestartet werden");
        return ESP_OK;
    }

    char buf[4096];
    int remaining = req->content_len;
    while (remaining > 0) {
        int r = httpd_req_recv(req, buf, std::min<int>(remaining, sizeof(buf)));
        if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (r <= 0) {
            esp_ota_abort(handle);
            if (s_display) s_display->setOutputEnabled(true);
            s_ota_in_progress = false;
            ESP_LOGE(TAG, "OTA receive failed (%d)", r);
            return ESP_FAIL;
        }
        err = esp_ota_write(handle, buf, r);
        if (err != ESP_OK) {
            esp_ota_abort(handle);
            if (s_display) s_display->setOutputEnabled(true);
            s_ota_in_progress = false;
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Fehler beim Schreiben der Firmware");
            return ESP_OK;
        }
        remaining -= r;
    }

    err = esp_ota_end(handle);
    if (err == ESP_OK) err = esp_ota_set_boot_partition(update);
    if (err != ESP_OK) {
        if (s_display) s_display->setOutputEnabled(true);
        s_ota_in_progress = false;
        ESP_LOGE(TAG, "OTA validation/boot selection failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Firmware ungueltig oder OTA fehlgeschlagen");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "OTA successful, rebooting");
    httpd_resp_sendstr(req, "OTA erfolgreich. myClock startet neu...");
    schedule_restart();
    return ESP_OK;
}
}

bool web_server_ota_in_progress() {
    return s_ota_in_progress;
}

bool web_server_start(ClockConfig* config, MatrixDisplay* display) {
    if (!config) return false;
    s_config = config;
    s_display = display;

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 10240;
    cfg.max_uri_handlers = 12;
    cfg.lru_purge_enable = true;

    esp_err_t err = httpd_start(&s_server, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return false;
    }

    httpd_uri_t root_uri{};
    root_uri.uri = "/";
    root_uri.method = HTTP_GET;
    root_uri.handler = root_handler;

    httpd_uri_t status_uri{};
    status_uri.uri = "/api/status";
    status_uri.method = HTTP_GET;
    status_uri.handler = status_handler;

    httpd_uri_t wifi_scan_uri{};
    wifi_scan_uri.uri = "/wifi/scan";
    wifi_scan_uri.method = HTTP_GET;
    wifi_scan_uri.handler = wifi_scan_handler;

    httpd_uri_t save_uri{};
    save_uri.uri = "/save";
    save_uri.method = HTTP_POST;
    save_uri.handler = save_handler;

    httpd_uri_t weather_uri{};
    weather_uri.uri = "/weather/update";
    weather_uri.method = HTTP_POST;
    weather_uri.handler = weather_update_handler;

    httpd_uri_t reboot_uri{};
    reboot_uri.uri = "/reboot";
    reboot_uri.method = HTTP_POST;
    reboot_uri.handler = reboot_handler;

    httpd_uri_t ota_uri{};
    ota_uri.uri = "/ota";
    ota_uri.method = HTTP_POST;
    ota_uri.handler = ota_handler;

    const httpd_uri_t* uris[] = {&root_uri, &status_uri, &wifi_scan_uri, &save_uri, &weather_uri, &reboot_uri, &ota_uri};
    for (const auto* uri : uris) {
        if (httpd_register_uri_handler(s_server, uri) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register URI %s", uri->uri);
            return false;
        }
    }

    WifiStatus ws{};
    wifi_manager_get_status(ws);
    ESP_LOGI(TAG, "Web interface started. Login: admin / configured admin password");
    if (ws.sta_connected) ESP_LOGI(TAG, "Open http://%s/", ws.ip);
    if (ws.fallback_ap) ESP_LOGI(TAG, "Setup AP: %s, open http://192.168.4.1/", ws.ap_ssid);
    return true;
}
