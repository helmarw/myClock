// Web Server for configuration and updates

static const char* serverHead PROGMEM =
  "<!DOCTYPE HTML><html><head>\n<title>myClock</title>\n"
  "<meta name='viewport' content='width=device-width, initial-scale=.83'>"
  "<link href='stylesheet.css' rel='stylesheet' type='text/css'></head>\n"
  "<body><a href='https://github.com/dragondaud/myClock' target='_blank'>\n"
  "<h1>myClock " VERSION "</h1></a>\n";

static const char* serverStyle PROGMEM =
  "body {background-color: DarkSlateGray; color: White; font-family: sans-serif;}\n"
  "a {text-decoration: none; color: LightSteelBlue;}\n"
  "a:hover {text-decoration: underline; color: SteelBlue;}\n"
  "div {min-width: 420px; max-width: 600px; border: ridge; padding: 10px; background-color: SlateGray;}\n"
  "td {padding: 4px; text-align: left;}\n"
  "th {padding: 4px; text-align: right;}\n"
  "input[type=range] {vertical-align: middle;}\n"
  "input[type=file]::-webkit-file-upload-button {padding:10px 15px; -webkit-border-radius: 5px;}\n"
  ".button {padding:10px 15px; background:#ccc; -webkit-border-radius: 5px;}\n"
  "meter {width: 400px; vertical-align: middle;}\n"
  "meter::after {content: attr(value); position:relative; top:-17px; color: Black;}\n"
  "meter::-webkit-meter-bar {background: none; background-color: LightBlue; "
  "box-shadow: 5px 5px 5px SlateGray inset; border: 1px solid; }\n"
  ".switch {position: relative; display: inline-block; width: 60px; height: 34px;}\n"
  ".switch input {opacity: 0; width: 0; height: 0;}\n"
  ".slider {position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0; background-color: #ccc; -webkit-transition: .4s; transition: .4s;}\n"
  ".slider:before {position: absolute; content: ''; height: 26px; width: 26px; left: 4px; bottom: 4px; background-color: white; -webkit-transition: .4s; transition: .4s;}\n"
  "input:checked + .slider {background-color: DarkSlateGray;}\n"
  "input:focus + .slider {box-shadow: 0 0 1px SlateGray;}\n"
  "input:checked + .slider:before {-webkit-transform: translateX(26px); -ms-transform: translateX(26px); transform: translateX(26px);}\n"
  ".slider.round {border-radius: 34px;}\n"
  ".slider.round:before {border-radius: 50%;}\n";

static const char* serverOptions PROGMEM =
  "<div><h3>%host%</h3>\n"
  "<form method='POST' action='/options' id='optionsForm' name='optionsForm'>\n"
  "<table><tr><th><label for='myColor'>Color</label></th>\n"
  "<td><input type='color' id='myColor' name='myColor' value='%myColor%'></td></tr>\n"
  "<tr><th><label for='brightness'>Brightness</label></th>\n"
  "<td><input type='number' id='brightNum' name='brightNum' style='width: 3em'"
  "min='1' max='255' value='%brightness%' oninput='brightness.value=brightNum.value'> \n"
  "<input type='range' id='brightness' name='brightness' "
  "min='1' max='255' value='%brightness%' oninput='brightNum.value=brightness.value'></td></tr>\n"
  "<tr><th><label for='threshold'>Threshold</label></th>\n"
  "<td><input type='number' id='threshNum' name='threshNum' style='width: 3em'"
  "min='1' max='1500' value='%threshold%' oninput='threshold.value=threshNum.value'> \n"
  "<input type='range' id='threshold' name='threshold' "
  "min='1' max='1500' value='%threshold%' oninput='threshNum.value=threshold.value'></td></tr>\n"
  "<tr><th><label for='milTime'>24hour Time</label></th>\n"
  "<td><label class='switch'><input type='checkbox' id='milTime' name='milTime' %milTime%>"
  "<span class='slider round'></span></label></td></tr>\n"
  "<tr><th><label for='daylightsaving'>Daylight savings</label></th>\n"
  "<td><label class='switch'><input type='checkbox' id='daylightsaving' name='daylightsaving' %daylightsaving%>"
  "<span class='slider round'></span></label></td></tr>\n"
  "<tr><th><label for='celsius'>Celsius</label></th>\n"
  "<td><label class='switch'><input type='checkbox' id='celsius' name='celsius' %celsius%>"
  "<span class='slider round'></span></label></td></tr>\n"
  "<tr><th><label for='t_offs'>Temp. Offset</label></th>\n"
  "<td><input type='number' id='t_offsNum' name='t_offsNum' style='width: 3em'"
  "min='-5' max='5' value='%t_offs%' oninput='t_offs.value=t_offsNum.value'> \n"
  "<input type='range' id='t_offs' name='t_offs' "
  "min='-5' max='5' value='%t_offs%' oninput='t_offsNum.value=t_offs.value'></td></tr>\n"
  "<tr><th><label for='language'>Language</label></th>\n"
  "<td><select name='language' id='language'>\n"
  "<option value='hr'>Croatian</option>\n"
  "<option value='cz'>Czech</option>\n"
  "<option value='nl'>Dutch</option>\n"
  "<option value='en'>English</option>\n"
  "<option value='fi'>Finnish</option>\n"
  "<option value='fr'>French</option>\n"
  "<option value='gl'>Galician</option>\n"
  "<option value='de'>German</option>\n"
  "<option value='hu'>Hungarian</option>\n"
  "<option value='it'>Italian</option>\n"
  "<option value='la'>Latvian</option>\n"
  "<option value='lt'>Lithuanian</option>\n"
  "<option value='pl'>Polish</option>\n"
  "<option value='pt'>Portuguese</option>\n"
  "<option value='sk'>Slovak</option>\n"
  "<option value='sl'>Slovenian</option>\n"
  "<option value='es'>Spanish</option></select></td></tr>\n"
  "<tr><th><label for='timezone'>Time Zone</label></th>\n"
  "<td><input type='text' id='timezone' name='timezone' value='%timezone%'></td></tr>\n"
  "<tr><th><label for='location'>Postal Code</label></th>\n"
  "<td><input type='text' id='location' name='location' size='10' value='%location%'></td></tr>\n"
  "<tr><th><label for='tzKey'>TimeZoneDB Key</label></th>\n"
  "<td><input type='text' id='tzKey' name='tzKey' value='%tzKey%'></td></tr>\n"
  "<tr><th><label for='owKey'>OpenWeatherMap Key</label></th>\n"
  "<td><input type='text' id='owKey' name='owKey' value='%owKey%'></td></tr>\n"
#ifdef SYSLOG
  "<tr><th><label for='syslogSrv'>SysLog Server</label></th>\n"
  "<td><input type='text' id='syslogSrv' name='owKey' value='%syslogSrv%'></td>  \n"
  "<th><label for='syslogPort'>Port</label></th>\n"
  "<td><input type='number' id='syslogPort' name='owKey' min='1' max='65535' value='%syslogPort%'></td></tr>\n"
#endif
  "<tr><th><label for='softAPpass'>Admin Password</label></th>\n"
  "<td><input type='password' id='softAPpass' name='softAPpass'\n"
  "minlength='8' placeholder='enter new password'></td></tr>\n"
  "<tr><td style='text-align: right'>Free Heap: %heap%</td>\n"
#ifdef LIGHT
  "<td>Light Level: %light%</td>\n"
#endif
  "<td><input type='submit' class='button' value='APPLY CONFIG' autofocus></td>\n"
  "</tr></table></form></div><p>\n";

static const char* serverUpdate PROGMEM =
  "<div><h3>Update Firmware</h3>\n"
  "<form method='POST' action='/update' enctype='multipart/form-data'>\n"
  "<input type='file' name='update'>\n"
  "<input type='submit' value='UPDATE' class='button'></form><p>\n"
  "<p><span><form method='GET' action='/reset' style='display:inline'>\n"
  "<input type='submit' value='REBOOT CLOCK' class='button'></form>\n"
  "<form method='GET' action='/download' style='display:inline'>\n"
  "<input type='submit' value='SAVE CONFIG' class='button'></form>\n"
  "<form method='GET' action='/logout' style='display:inline'>\n"
  "<input type='submit' value='LOGOUT' class='button'></form></span>\n"
  "<p></div></body></html>\n";

static const char* serverReboot PROGMEM =
  "<!DOCTYPE HTML><html><head>\n"
  "<meta http-equiv=\"refresh\" content=\"15;url=/\" />"
  "<style>body {background-color: DarkSlateGray; color: White;}"
  "</style></head>\n"
  "<body><h1>myClock " VERSION "</h1>"
  "Rebooting...</body></html>";

static const char* textPlain PROGMEM = "text/plain";
static const char* textHtml PROGMEM = "text/html";
static const char* textCss PROGMEM = "text/css";
static const char* checked PROGMEM = "checked";

void handleNotFound() {
#ifdef SYSLOG
  syslog.log(F("webServer: Not Found"));
#endif
  server.sendHeader(F("Location"), F("/"));
  server.send(301);
}

bool handleAuth() {
  return server.authenticate(ADMIN_USER, softAPpass.c_str());
}

void reqAuth() {
  return server.requestAuthentication(DIGEST_AUTH, HOST);
}

void handleStyle() {
  if (!handleAuth()) return reqAuth();
  server.send(200, textCss, String(serverStyle));
}

void handleOptions() {
  if (!handleAuth()) return reqAuth();
  if (!server.hasArg(F("myColor"))) return server.send(503, textPlain, F("FAILED"));
  String c = server.arg(F("myColor"));
  if (c != "") myColor = htmlColor565(c);
  uint8_t b = server.arg(F("brightness")).toInt();
  if (b) brightness = b;
  else brightness = 255;
  int t = server.arg(F("threshold")).toInt();
  if (t) threshold = t;
  else threshold = 500;
  int o = server.arg(F("t_offs")).toInt();
  if (o) t_offs = o;
  else t_offs = 0;  
  c = server.arg(F("milTime"));
  if (c == "on") milTime = true;
  else milTime = false;
  c = server.arg(F("daylightsaving"));
  if (c == "on") daylightsaving = true;
  else daylightsaving = false;
  c = server.arg(F("celsius"));
  if (c == "on") celsius = true;
  else celsius = false;
  c = server.arg(F("language"));
  if (c != "") language = c;
  c = server.arg(F("softAPpass"));
  if (c != "") softAPpass = c;
  c = server.arg(F("location"));
  if (c != "") location = c;
  c = server.arg(F("timezone"));
  if (c != "") {
    c.trim();
    c.replace(' ', '_');
    if (timezone != c) {
      timezone = c;
      setNTP(timezone);
      delay(1000);
    }
  }
  c = server.arg(F("tzKey"));
  if (c != "") tzKey = c;
  c = server.arg(F("owKey"));
  if (c != "") owKey = c;
  displayDraw(brightness);
  getWeather();
  writeSPIFFS();
  server.sendHeader(F("Location"), F("/"));
  server.send(301);
}

void handleRoot() {
  if (!handleAuth()) return reqAuth();
  size_t fh = ESP.getFreeHeap();
#ifdef SYSLOG
  syslog.log(F("webServer: root"));
#endif
  server.sendHeader(F("Connection"), F("close"));
  time_t now = time(nullptr);
  String t = ctime(&now);
  t.trim();
  t = String(F("<span style='float: right'>")) + t + String(F("</span>"));
  char c[8];
  sprintf(c, "#%06X", color565to888(myColor));
  String payload = String(serverHead);
#ifdef DS18
  payload += PSTR("<p><meter value='") + String(Temp) + PSTR("' min='-50' max='150'></meter> Temperature: "+ String(Temp) + "\n");
#endif
  payload += String(serverOptions);
  payload.replace(F("%host%"), String(HOST) + t);
  payload.replace(F("%myColor%"), String(c));
  payload.replace(F("%brightness%"), String(brightness));
  payload.replace(F("%threshold%"), String(threshold));
  payload.replace(F("%milTime%"), milTime ? checked : "");
  payload.replace(F("%daylightsaving%"), daylightsaving ? checked : "");
  payload.replace(F("%celsius%"), celsius ? checked : "");
  payload.replace(F("%t_offs%"), String(t_offs));
  payload.replace(PSTR("'") + String(language) + PSTR("'"),
                  PSTR("'") + String(language) + PSTR("'") + PSTR(" selected"));
  payload.replace(F("%location%"), String(location));
  payload.replace(F("%timezone%"), String(timezone));
  payload.replace(F("%tzKey%"), String(tzKey));
  payload.replace(F("%owKey%"), String(owKey));
#ifdef SYSLOG
  payload.replace(F("%syslogSrv%"), String(syslogSrv));
  payload.replace(F("%syslogPort%"), String(syslogPort));
#endif
  payload.replace(F("%heap%"), String(fh));
#ifdef LIGHT
  payload.replace(F("%light%"), String(light));
#endif
  payload += String(serverUpdate);
  server.send(200, textHtml, payload);
}

void handleReset() {
  if (!handleAuth()) return reqAuth();
#ifdef SYSLOG
  syslog.log(F("webServer: reset"));
#endif
  OUT.println(F("webServer: reset"));
  server.send(200, textHtml, serverReboot);
  server.close();
  delay(1000);
  ESP.restart();
}

void handleDownload() { // send running config as JSON object to browser
  if (!handleAuth()) return reqAuth();
  String payload = getSPIFFS();
  server.sendHeader(F("Content-Disposition"), F("attachment; filename=config.json"));
  server.send(200, F("application/json"), payload);
}

void handleLogout() {
  server.send(401, textPlain, F("logged out"));
}

void startWebServer() {
  server.on(F("/"), HTTP_GET, handleRoot);
  server.on(F("/stylesheet.css"), HTTP_GET, handleStyle);
  server.on(F("/options"), handleOptions);
  server.on(F("/reset"), HTTP_GET, handleReset);
  server.on(F("/download"), HTTP_GET, handleDownload);
  server.on(F("/logout"), HTTP_GET, handleLogout);
  server.on(F("/favicon.ico"), HTTP_GET, []() {
    server.sendHeader(F("Location"), F("https://www.arduino.cc/favicon.ico"));
    server.send(301);
  });
  server.on(F("/update"), HTTP_POST, []() { // upload and flash new image
    if (!handleAuth()) return reqAuth();
#ifdef SYSLOG
    syslog.log(F("webServer: update"));
#endif
    server.send(200, textPlain, (Update.hasError()) ? F("FAIL") : F("OK"));
    server.close();
    delay(1000);
    ESP.restart();
  }, []() {
    if (!handleAuth()) return reqAuth();
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      display_ticker.detach();
#if defined(ESP8266)
      WiFiUDP::stopAll();
#else
      MDNS.end();
#endif
      OUT.printf_P(PSTR("Update: %s\n"), upload.filename.c_str());
      uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
      if (!Update.begin(maxSketchSpace)) { //start with max available size
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) { //true to set the size to the current progress
        OUT.printf_P(PSTR("Update Success: %u\nRebooting...\n"), upload.totalSize);
      } else {
        Update.printError(Serial);
      }
    }
    yield();
  }); // server.on("/update")
  server.onNotFound(handleNotFound);
  server.begin();
  MDNS.addService(F("_http"), F("_tcp"), 80);
}
