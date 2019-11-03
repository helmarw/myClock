// SPIFFS config file
// auto format if unable to write

void readSPIFFS() {
#if defined(ESP8266)
  if (SPIFFS.begin()) {     // default will autoformat SPIFFS
#else
  if (SPIFFS.begin(true)) { // autoformat SPIFFS
#endif
    OUT.println(F("readSPIFFS: mounted"));
    if (SPIFFS.exists(F("/config.json"))) {
      File configFile = SPIFFS.open(F("/config.json"), "r");
      if (!configFile) return;
      size_t size = configFile.size();
      std::unique_ptr<char[]> buf(new char[size]);
      configFile.readBytes(buf.get(), size);
      DynamicJsonBuffer jsonBuffer;
      JsonObject& json = jsonBuffer.parseObject(buf.get());
      parseJson(json);
      configFile.close();
    }
  } else {
    OUT.println(F("readSPIFFS: failed to mount SPIFFS"));
  }
}

bool parseJson(JsonObject& json) {  // parse JSON object to running config
  if (json.success()) {
    String sp = json["softAPpass"];
    if (sp.length() >= 8) softAPpass = sp;
    String lo = json["location"];
    if (lo != "") location = lo;
    String tz = json["timezone"];
    if (tz != "") timezone = tz;
    String tk = json["tzKey"];
    if (tk != "") tzKey = tk;
    String ow = json["owKey"];
    if (ow != "") owKey = ow;
    String la = json["language"];
    if (la != "") language = la;
    brightness = json["brightness"];
    milTime = json["milTime"];
    myColor = json["myColor"];
    threshold = json["threshold"];
    celsius = json["celsius"];
    t_offs = json["t_offs"]; //tempsensor offset
#ifdef SYSLOG
    String sl = json["syslogSrv"];
    if (sl != "") syslogSrv = sl;
    syslogPort = json["syslogPort"];
#endif
  }
}

void writeSPIFFS() {  // convert running config to JSON object and write flash config file
  DynamicJsonBuffer jsonBuffer;
  JsonObject& json = jsonBuffer.createObject();
  json["softAPpass"] = softAPpass;
  json["location"] = location;
  json["timezone"] = timezone;
  json["tzKey"] = tzKey;
  json["owKey"] = owKey;
  json["brightness"] = brightness;
  json["milTime"] = milTime;
  json["myColor"] = myColor;
  json["threshold"] = threshold;
  json["celsius"] = celsius;
  json["language"] = language;
  json["t_offs"] = t_offs; //tempsensor offset
#ifdef SYSLOG
  json["syslogSrv"] = syslogSrv;
  json["syslogPort"] = syslogPort;
#endif
  File configFile = SPIFFS.open("/config.json", "w");
  if (!configFile) {
    display.setCursor(2, row2);
    display.print(F("config failed"));
    OUT.println(F("failed to open config.json for writing"));
    delay(5000);
    ESP.restart();
  } else {
#ifdef SYSLOG
    syslog.log(F("save config"));
#endif
    json.printTo(configFile);
    configFile.close();
    delay(1000);
  }
}

String getSPIFFS() {  // read config file from flash and return JSON object
  String payload;
  if (SPIFFS.exists("/config.json")) {
    File configFile = SPIFFS.open("/config.json", "r");
    if (configFile) {
      size_t size = configFile.size();
      std::unique_ptr<char[]> buf(new char[size]);
      configFile.readBytes(buf.get(), size);
      DynamicJsonBuffer jsonBuffer;
      JsonObject& json = jsonBuffer.parseObject(buf.get());
      if (json.success()) json.prettyPrintTo(payload);
      configFile.close();
    }
  }
  return payload;
}
