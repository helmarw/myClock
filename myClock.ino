/*   myClock -- ESP8266 WiFi NTP Clock for pixel displays
     Copyright (c) 2019 David M Denney <dragondaud@gmail.com>
     distributed under the terms of the MIT License
     Switching between ESP8266 and ESP32 platform requires deleting preferences.txt which the update script will do automatically when updating core.
     Designed to run on a Wemos-D1-Mini or NodeMCU, configured for CPU Freq 160Mhz and Flash size 4M (1M SPIFFS). 
     notice: i tried four different ESP8266 boards, and non of them i got to work, so better use an ESP32, e.g. MH-ET LIVE D1 mini, that works for me at least
     for resetting flash in ESP use "esptool.py --port /dev/cu.SLAB_USBtoUART  erase_flash" or simply "esptool.py erase_flash" will choose port automatically
     use "pip install esptool" for installing esptool in commandline    
*/

//  ESP8266 version 2.5.2 required https://github.com/esp8266/Arduino
//  https://github.com/esp8266/Arduino#using-git-version-basic-instructions

//  ESP32 version 1.0.4 required https://github.com/espressif/arduino-esp32
//  https://github.com/espressif/arduino-esp32#installation-instructions

//#include <ArduinoJson.h>        // works with version 5.13.5 (doesnt work with >=6) https://github.com/bblanchon/ArduinoJson/releases/latest
#include "ArduinoJson/ArduinoJson_5x.h"  //included for comptibility reasons
#include <WiFiManager.h>        // works with version 1.0.0 https://github.com/tzapu/WiFiManager/tree/development
#include <ArduinoOTA.h>
#include <FS.h>
#include <time.h>
#include "display.h"

#if defined(ESP8266)
#include <ESP8266mDNS.h>
#include <ESP8266HTTPClient.h>
ESP8266WebServer server(80);
#else
#include <ESPmDNS.h>
#include <SPIFFS.h>
#include <HTTPClient.h>
WebServer server(80);
#endif

#define APPNAME "myClock"
#define VERSION "0.10.4 Mod by HW"
#define ADMIN_USER "admin"    // WebServer logon username
#define DS18                // enable DS18B20 temperature sensor
//#define SYSLOG              // enable SYSLOG support
#define LIGHT               // enable LDR light sensor
#define WDELAY 900          // delay 15 min between weather updates
#define myOUT 1             // {0 = NullStream, 1 = Serial, 2 = Bluetooth}

#if myOUT == 0                    // NullStream output
NullStream NullStream;
Stream & OUT = NullStream;
#elif myOUT == 2                  // Bluetooth output, only on ESP32
#include "BluetoothSerial.h"
BluetoothSerial SerialBT;
Stream & OUT = SerialBT;
#else                             // Serial output default
Stream & OUT = Serial;
#endif

String tzKey = "fill_in_your_own";    // API key from https://timezonedb.com/register
String owKey = "fill_in_your_own";  // API key from https://home.openweathermap.org/api_keys
String softAPpass = "ChangeMe";   // password for SoftAP config and WebServer logon, minimum 8 characters
uint8_t brightness = 255;         // 0-255 display brightness default 255
bool milTime = true;              // set false for 12hour clock
String location;                  // zipcode or empty for geoIP location
String timezone;                  // timezone from https://timezonedb.com/time-zones or empty for geoIP
int threshold = 1000;              // below this value display will dim, incrementally
bool celsius = true;             // set true to display temp in celsius
bool daylightsaving = true;      // set true to display daylightsavings time true=+1hr, false=+0hrs
String language = "en";           // font does not support all languages
String countryCode = "DE";        // default US, automatically set based on public IP address
static const char wday_name[][4] = {
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
  };
static const char mon_name[][4] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
  };
String description;
int Temp = 0;
int id;
int t_offs = 0; //Temperature offset if the sensores are calibrated, otherwise default 0 (no offset)

// Syslog server wireless debugging and monitoring
#ifdef SYSLOG
#include <Syslog.h>             // https://github.com/arcao/Syslog
WiFiUDP udpClient;
Syslog syslog(udpClient, SYSLOG_PROTO_IETF);
String syslogSrv = "syslog";
uint16_t syslogPort = 514;
#endif

// DS18B20 temperature sensor for local/indoor temp display
#ifdef DS18
#include <OneWire.h>
#include <DallasTemperature.h>
#define ONE_WIRE_BUS 17 // D3 (or 0) for Wemos D1 mini (and WEMOS D1 ESP32) or 17 for ESP32-DEV and similar
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
#endif

static const char* UserAgent PROGMEM = "myClock/1.0 (Arduino ESP8266)";

time_t TWOAM, pNow, wDelay;
uint8_t pHH, pMM, pSS;
uint16_t light = threshold;
long offset;
char HOST[20];
uint8_t dim;

void setup() {
#if myOUT == 0
  Serial.end(); // close serial if not used
#else
  Serial.begin(115200);
  while (!Serial) delay(10);  // wait for Serial to start
  Serial.println();
#endif

#if myOUT == 2
  if (!SerialBT.begin(APPNAME)) {
    Serial.println(F("Bluetooth failed"));
    delay(5000);
    ESP.restart();
  }
  delay(5000); // wait for client to connect
#endif

  readSPIFFS(); // fetch stored configuration

  display.begin(16); //default for pxMatrix <=1.3.0 but PxMatrix.h needs to be modified for cutom ISP and setMuxDelay, added as PxMatrix_mod.h)
  //display.begin(16, CLK, MOSI, MISO, SS); //for pxMatrix >=v1.6.0
  //display.begin(16, 14, 13, 12, 4); // for Wemos D1 mini
 // display.begin(16, 18, 23, 19, 21);  // for MH-ET LIVE D1 mini 
  display.setMuxDelay(2,2,2,2,2);  //added for slow muxer
  display_ticker.attach(0.002, display_updater);
  display.clearDisplay();
  display.setFont(&TomThumb);
  display.setTextWrap(false);
  display.setTextColor(myColor);
  display.setBrightness(brightness);

  drawImage(0, 0); // display splash image while connecting and setting time

#ifdef DS18
  sensors.begin();  // start temp sensor
  delay(150);
#endif

  startWiFi();

#ifdef SYSLOG
  syslog.server(syslogSrv.c_str(), syslogPort); // configure syslog server
  syslog.deviceHostname(HOST);
  syslog.appName(APPNAME);
  syslog.defaultPriority(LOG_INFO);
#endif

  if (location == "") location = getIPlocation(); // get postal code and/or timezone as needed
  else while (timezone == "") getIPlocation();

  display.clearDisplay();       // display hostname, ip address, timezone, version
  display.setCursor(2, row1);
  display.setTextColor(myGREEN);
  display.print(HOST);
  display.setCursor(2, row2);
  display.setTextColor(myBLUE);
  display.print(WiFi.localIP());
  display.setCursor(2, row3);
  display.setTextColor(myMAGENTA);
  display.print(timezone);
  display.setCursor(2, row4);
  display.setTextColor(myLTBLUE);
  display.print(F("V"));
  display.print(VERSION);
  display.setCursor(32, row4);
  display.setTextColor(myBLUE);
  OUT.printf_P(PSTR("setup: %s, %s, %s \r\n"), location.c_str(), timezone.c_str(), milTime ? "true" : "false");
#ifdef SYSLOG
  syslog.logf("setup: %s|%s|%s", location.c_str(), timezone.c_str(), milTime ? "true" : "false");
#endif
  setNTP(timezone); // configure NTP from timezone name for location
  delay(1000);
  startWebServer();
  displayDraw(brightness);  // initial time display before fetching weather and starting loop
  description = getWeather();
} // setup

void loop() {
#if defined(ESP8266)
  MDNS.update();    // ESP32 does this automatically
#endif
  ArduinoOTA.handle();    // check for OTA updates
  server.handleClient();  // handle WebServer requests
  struct tm * timeinfo;
  time_t now = time(nullptr); // get current time
  timeinfo = localtime(&now);
  if (now != pNow) { // skip ahead if still same time
  //  if (now > TWOAM) setNTP(timezone);  // recheck timezone every day at 2am
    int ss = timeinfo->tm_sec;
    int mm = timeinfo->tm_min;
    int hh = timeinfo->tm_hour;
    if (daylightsaving) hh = hh + 1;
    int wd = timeinfo->tm_wday;
    int dd = timeinfo->tm_mday;
    int mon = timeinfo->tm_mon;
    int my = mon+1;
    int yy = timeinfo->tm_year;
    yy = yy + 1900;
    if ((!milTime) && (hh > 12)) hh -= 12;
    if (hh > 23) hh = 0;
    if (hh > 24) hh = 1;
    if (ss != pSS) {    // only update seconds if changed
      int s0 = ss % 10;
      int s1 = ss / 10;
      if (s0 != digit0.Value()) digit0.Morph(s0);
      if (s1 != digit1.Value()) digit1.Morph(s1);
      pSS = ss;
        int i = id / 100;  // id from getWeather()
        switch (i) {
          case 2: // Thunderstorms
            display.setTextColor(myORANGE);
            break;
          case 3: // Drizzle
            display.setTextColor(myBLUE);
            break;
          case 5: // Rain
            display.setTextColor(myBLUE);
            break;
          case 6: // Snow
            display.setTextColor(myWHITE);
            break;
          case 7: // Atmosphere
            display.setTextColor(myYELLOW);
            break;
          case 8: // Clear/Clouds
            display.setTextColor(myGRAY);
            break;
        }
       if (ss == 0 || ss == 20 || ss == 40) {
       int16_t  x1, y1, ww;
       uint16_t w, h;
       String date = ( String(wday_name[wd]) + " "+ String(dd, DEC) + ". " + String(mon_name[mon]) + " " + String(yy,DEC));
       display.getTextBounds(date, 0, row4, &x1, &y1, &w, &h);
       display.fillRect(0, 25, 64, 7, myBLACK);
       if (w < 64) x1 = (68 - w) >> 1;         // center Date (getTextBounds returns too long)
       display.setCursor(x1, row4);
       display.print(date);
       }
       if (ss == 10 || ss == 30 || ss == 50) {
       int16_t  x1, y1, ww;
       uint16_t w, h;
       display.getTextBounds(description, 0, row4, &x1, &y1, &w, &h);
       display.fillRect(0, 25, 64, 7, myBLACK);
       if (w < 64) x1 = (68 - w) >> 1;         // center weather description (getTextBounds returns too long)
       display.setCursor(x1, row4);
       display.print(description);
       }
#ifdef LIGHT
      getLight();
#endif
    }
    if (mm != pMM) {    // update minutes, if changed
      int m0 = mm % 10;
      int m1 = mm / 10;
      if (m0 != digit2.Value()) digit2.Morph(m0);
      if (m1 != digit3.Value()) digit3.Morph(m1);
      pMM = mm;
      OUT.printf_P(PSTR("%02d:%02d %3s %02d.%02d.%04d LDR:%d MEM:%d %s DS18:%d \n"), hh, mm, wday_name[wd], dd, my, yy, light, ESP.getFreeHeap(), description.c_str(), Temp); // output debug once per minute
    }
    if (hh != pHH) {    // update hours, if changed
      setNTP(timezone);
      int h0 = hh % 10;
      int h1 = hh / 10;
      if (h0 != digit4.Value()) digit4.Morph(h0);
      if (h1 != digit5.Value()) digit5.Morph(h1);
      pHH = hh;
    }
#ifdef DS18
    if (ss == 3 || ss == 13 || ss == 23 || ss == 33 || ss == 43 || ss == 53) { //dont querry sensor to often, gives wrong result otherwise, here every 10sec
    sensors.requestTemperatures();
    int t;
    if (celsius == true) t = (int)round(sensors.getTempCByIndex(0));
    if (celsius == false) t = (int)round(sensors.getTempFByIndex(0));
    if (t < -10 | t > 99) t = Temp;
      Temp = t + t_offs;
      int tc;
        if (celsius) tc = (int)round(Temp * 1.8) + 32;
        else tc = (int)round(Temp);
        if (tc <= 32) display.setTextColor(myCYAN);         // top row color based on temperature
        else if (tc <= 50) display.setTextColor(myLTBLUE);
        else if (tc <= 60) display.setTextColor(myBLUE);
        else if (tc <= 78) display.setTextColor(myGREEN);
        else if (tc <= 86) display.setTextColor(myYELLOW);
        else if (tc <= 95) display.setTextColor(myORANGE);
        else if (tc > 95) display.setTextColor(myRED);
        else display.setTextColor(myColor);
      display.fillRect(0, 0, 10, 6, myBLACK);
      display.setCursor(3, row1);
      display.printf_P(PSTR("%2d"), Temp);
    }
#endif
    pNow = now;
    if (now > wDelay) description = getWeather(); // fetch weather again if enough time has passed
  }
} // loop

void displayDraw(uint8_t b) { // clear display and draw all digits of current time
  display.clearDisplay();
  display.setBrightness(b);
  dim = b;
  time_t now = time(nullptr);
  int ss = now % 60;
  int mm = (now / 60) % 60;
  int hh = ((now / (60 * 60)) % 24);  
  if ((!milTime) && (hh > 12)) hh -= 12;
  OUT.printf_P(PSTR("%02d:%02d\r"), hh, mm);
  digit1.DrawColon(myColor);
  digit3.DrawColon(myColor);
  digit0.Draw(ss % 10, myColor);
  digit1.Draw(ss / 10, myColor);
  digit2.Draw(mm % 10, myColor);
  digit3.Draw(mm / 10, myColor);
  digit4.Draw(hh % 10, myColor);
  digit5.Draw(hh / 10, myColor);
  pNow = -10; //test for resetting clock dispaly after getting NTP time
  pHH = -10;
}

#ifdef LIGHT
void getLight() {                   // if LDR present, auto-dim display below threshold
  int lt = analogRead(A0);
  if (lt > 20) {                    // ignore LDR if value invalid
    light = (light * 3 + lt) >> 2;  // average sensor into light slowly
    if (light >= threshold) dim = brightness;                 // light above threshold, full brightness
    else if (light < (threshold >> 3)) dim = brightness >> 4; // light below 1/8 threshold, brightness 1/16
    else if (light < (threshold >> 2)) dim = brightness >> 3; // light below 1/4 threshold, brightness 1/8
    else if (light < (threshold >> 1)) dim = brightness >> 2; // light below 1/2 threshold, brightness 1/4
    else if (light < threshold) dim = brightness >> 1;        // light below threshold, brightness 1/2
    display.setBrightness(dim);
  }
}
#endif
