// webui.cpp
// Serves the settings page and the few small data endpoints it talks to.

#include "webui.h"
#include "webpage.h"
#include "settings.h"
#include "display.h"
#include "weather.h"
#include "alarms.h"
#include "logbuf.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <time.h>

static AsyncWebServer server(80);
static uint32_t g_restartAtMs = 0;
static bool     g_forgetWifi  = false;

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

static String colorToHex(uint32_t rgb) {
  char buf[8];
  snprintf(buf, sizeof(buf), "#%06X", (unsigned)(rgb & 0xFFFFFF));
  return String(buf);
}

static uint32_t hexToColor(const String &text, uint32_t fallback) {
  String s = text;
  s.trim();
  if (s.startsWith("#")) s = s.substring(1);
  if (s.length() != 6) return fallback;
  char *end = nullptr;
  unsigned long value = strtoul(s.c_str(), &end, 16);
  if (end && *end != '\0') return fallback;
  return (uint32_t)(value & 0xFFFFFF);
}

static String param(AsyncWebServerRequest *request, const char *name,
                    const String &fallback) {
  if (request->hasParam(name, true)) return request->getParam(name, true)->value();
  if (request->hasParam(name))       return request->getParam(name)->value();
  return fallback;
}

static bool paramBool(AsyncWebServerRequest *request, const char *name, bool fallback) {
  String v = param(request, name, fallback ? "1" : "0");
  return (v == "1" || v == "true" || v == "on");
}

static long paramLong(AsyncWebServerRequest *request, const char *name, long fallback) {
  String v = param(request, name, String(fallback));
  return v.toInt();
}

static String uptimeText() {
  unsigned long seconds = millis() / 1000UL;
  unsigned long hours   = seconds / 3600UL;
  unsigned long minutes = (seconds / 60UL) % 60UL;
  return String(hours) + "h " + String(minutes) + "m " + String(seconds % 60UL) + "s";
}

// ---------------------------------------------------------------------------
// Endpoints
// ---------------------------------------------------------------------------

static void sendStatus(AsyncWebServerRequest *request) {
  DynamicJsonDocument doc(768);

  bool online = (WiFi.status() == WL_CONNECTED);
  doc["online"] = online;
  doc["ssid"]   = online ? WiFi.SSID() : String("");
  doc["rssi"]   = online ? WiFi.RSSI() : 0;
  doc["ip"]     = online ? WiFi.localIP().toString() : String("none");
  doc["host"]   = deviceHostname() + ".local";
  doc["ntp"]    = cfg.ntpServer;

  struct tm now;
  bool haveTime = getLocalTime(&now, 50);
  char timeBuf[40] = "not set yet";
  if (haveTime) strftime(timeBuf, sizeof(timeBuf), "%a %d %b %Y  %H:%M:%S", &now);
  doc["time"]   = timeBuf;
  doc["synced"] = haveTime && (now.tm_year + 1900) > 2020;

  WeatherData wx;
  weatherGet(wx);
  doc["wxok"] = wx.valid;
  doc["wx"]   = (wx.place.length() ? wx.place : cfg.zip) + "  " + wx.status;

  doc["uptime"]   = uptimeText();
  doc["restarts"] = logRestartCount();
  doc["restart"]  = logRestartSummary();
  doc["heap"]    = (uint32_t)ESP.getFreeHeap();
  doc["version"] = FW_VERSION;

  String out;
  serializeJson(doc, out);
  request->send(200, "application/json", out);
}

static void sendSettings(AsyncWebServerRequest *request) {
  DynamicJsonDocument doc(1536);

  doc["version"] = FW_VERSION;
  doc["ssid"]    = cfg.wifiSsid.length() ? cfg.wifiSsid : WiFi.SSID();
  doc["ntp"]     = cfg.ntpServer;
  doc["tz"]      = cfg.timeZone;
  doc["tzname"]  = cfg.timeZoneName;

  doc["h24"]   = cfg.use24Hour;
  doc["secs"]  = cfg.showSeconds;
  doc["ampm"]  = cfg.showAmPm;
  doc["blink"] = cfg.blinkColon;
  doc["date"]  = cfg.showDate;
  doc["face"]  = cfg.clockFace;
  doc["ghost"]  = cfg.ghostSegments;
  doc["invert"] = cfg.invertColors;
  doc["stroke"] = cfg.segmentStroke;
  doc["fg"]    = colorToHex(cfg.colorText);
  doc["bg"]    = colorToHex(cfg.colorBack);

  doc["bri"]     = cfg.brightness;
  doc["nbri"]    = cfg.nightBrightness;
  doc["autodim"] = cfg.autoDim;

  doc["zip"]     = cfg.zip;
  doc["country"] = cfg.country;
  doc["metric"]  = cfg.metric;
  doc["wxdays"]  = cfg.forecastDays;
  doc["wxcur"]   = cfg.showCurrentScreen;
  doc["wxhr"]    = cfg.showHourlyScreen;
  doc["wxday"]   = cfg.showDailyScreen;

  doc["alsound"] = cfg.alarmSound;
  doc["alvol"]   = cfg.alarmVolume;
  doc["alcustom"] = cfg.customRingtone;
  {
    JsonArray list = doc.createNestedArray("tones");
    for (int i = 0; i < RINGTONE_COUNT; i++) list.add(RINGTONES[i].label);
  }

  JsonArray alarms = doc.createNestedArray("alarms");
  for (int i = 0; i < ALARM_COUNT; i++) {
    JsonObject a = alarms.createNestedObject();
    a["on"]     = cfg.alarms[i].enabled;
    a["h"]      = cfg.alarms[i].hour;
    a["m"]      = cfg.alarms[i].minute;
    a["days"]   = cfg.alarms[i].days;
    a["tone"]   = cfg.alarms[i].useTone;
    a["led"]    = cfg.alarms[i].useLed;
    a["screen"] = cfg.alarms[i].useScreen;
  }

  String out;
  serializeJson(doc, out);
  request->send(200, "application/json", out);
}

static void handleSave(AsyncWebServerRequest *request) {
  // Remember the values that decide what has to happen after the save.
  String oldSsid    = cfg.wifiSsid;
  String oldNtp     = cfg.ntpServer;
  String oldZone    = cfg.timeZone;
  String oldZip     = cfg.zip;
  String oldCountry = cfg.country;
  bool   oldMetric  = cfg.metric;
  uint8_t oldDays   = cfg.forecastDays;

  String newSsid = param(request, "ssid", cfg.wifiSsid);
  String newPass = param(request, "pass", "");
  if (newSsid.length()) cfg.wifiSsid = newSsid;
  if (newPass.length()) cfg.wifiPass = newPass;

  cfg.ntpServer    = param(request, "ntp", cfg.ntpServer);
  cfg.timeZone     = param(request, "tz", cfg.timeZone);
  cfg.timeZoneName = param(request, "tzname", cfg.timeZoneName);

  cfg.use24Hour   = paramBool(request, "h24",   cfg.use24Hour);
  cfg.showSeconds = paramBool(request, "secs",  cfg.showSeconds);
  cfg.showAmPm    = paramBool(request, "ampm",  cfg.showAmPm);
  cfg.blinkColon  = paramBool(request, "blink", cfg.blinkColon);
  cfg.showDate    = paramBool(request, "date",  cfg.showDate);
  cfg.clockFace     = (uint8_t)paramLong(request, "face", cfg.clockFace);
  cfg.ghostSegments = paramBool(request, "ghost", cfg.ghostSegments);
  cfg.invertColors  = paramBool(request, "invert", cfg.invertColors);
  cfg.segmentStroke = (uint8_t)constrain(paramLong(request, "stroke", cfg.segmentStroke), 0, 3);

  cfg.colorText = hexToColor(param(request, "fg", colorToHex(cfg.colorText)), cfg.colorText);
  cfg.colorBack = hexToColor(param(request, "bg", colorToHex(cfg.colorBack)), cfg.colorBack);

  cfg.brightness      = (uint8_t)constrain(paramLong(request, "bri",  cfg.brightness), 5, 255);
  cfg.nightBrightness = (uint8_t)constrain(paramLong(request, "nbri", cfg.nightBrightness), 5, 255);
  cfg.autoDim         = paramBool(request, "autodim", cfg.autoDim);

  cfg.zip     = param(request, "zip", cfg.zip);
  cfg.country = param(request, "country", cfg.country);
  cfg.metric      = (paramLong(request, "metric", cfg.metric ? 1 : 0) == 1);
  cfg.forecastDays = (uint8_t)constrain(paramLong(request, "wxdays", cfg.forecastDays), 3, MAX_FORECAST_DAYS);
  cfg.showCurrentScreen = paramBool(request, "wxcur",  cfg.showCurrentScreen);
  cfg.showHourlyScreen  = paramBool(request, "wxhr",   cfg.showHourlyScreen);
  cfg.showDailyScreen   = paramBool(request, "wxday",  cfg.showDailyScreen);

  cfg.alarmSound  = (uint8_t)constrain(paramLong(request, "alsound", cfg.alarmSound), 1, SOUND_CUSTOM);
  cfg.alarmVolume = (uint8_t)constrain(paramLong(request, "alvol",   cfg.alarmVolume), 0, 100);
  cfg.customRingtone = param(request, "alcustom", cfg.customRingtone);

  for (int i = 0; i < ALARM_COUNT; i++) {
    String prefix = "a" + String(i);
    AlarmConfig &a = cfg.alarms[i];
    a.enabled = paramBool(request, (prefix + "en").c_str(), a.enabled);

    String when = param(request, (prefix + "time").c_str(), "");
    if (when.length() >= 4 && when.indexOf(':') > 0) {
      a.hour   = (uint8_t)constrain(when.substring(0, when.indexOf(':')).toInt(), 0, 23);
      a.minute = (uint8_t)constrain(when.substring(when.indexOf(':') + 1).toInt(), 0, 59);
    }
    a.days      = (uint8_t)constrain(paramLong(request, (prefix + "d").c_str(), a.days), 0, 127);
    a.useTone   = paramBool(request, (prefix + "t").c_str(), a.useTone);
    a.useLed    = paramBool(request, (prefix + "l").c_str(), a.useLed);
    a.useScreen = paramBool(request, (prefix + "s").c_str(), a.useScreen);
  }

  if (cfg.clockFace < FACE_SEVENSEG || cfg.clockFace > FACE_ITALIC) {
    cfg.clockFace = FACE_SEVENSEG;
  }

  // A postcode is not a position. The clock looks the position up once and
  // keeps it, so when the postcode changes the position it is holding belongs
  // to the old one and has to go with it.
  //
  // This has to happen before the save, not after. Saving writes the position
  // out along with everything else, so clearing it afterwards left the stored
  // settings holding the new postcode next to the old position, still marked
  // as known. Nothing looked the new postcode up after that, on this run or
  // any run after it, because as far as the clock could tell it already knew
  // where it was.
  bool placeChanged = (cfg.zip != oldZip || cfg.country != oldCountry);
  if (placeChanged) {
    cfg.haveLocation = false;
    cfg.latitude     = 0.0;
    cfg.longitude    = 0.0;
    cfg.placeName    = "";
    logLine("Postcode changed to " + cfg.country + " " + cfg.zip +
            ", forgetting the old position");
  }

  settingsSave();
  logLine("Settings saved from the web page");

  // Put the new values to work without needing a restart where we can.
  if (cfg.ntpServer != oldNtp || cfg.timeZone != oldZone) {
    configTzTime(cfg.timeZone.c_str(), cfg.ntpServer.c_str(), "pool.ntp.org", "time.google.com");
  }
  if (placeChanged) {
    // Throw away the readings too. They describe somewhere else, and showing
    // another town's weather under a new postcode is worse than showing none.
    weatherForget();
    weatherRequestRefresh(true);
  } else if (cfg.metric != oldMetric || cfg.forecastDays != oldDays) {
    weatherRequestRefresh(false);
  }
  uiSetBrightness(cfg.brightness);
  uiApplyInvert();
  uiRedraw();

  // Only a change of WiFi network needs a restart.
  bool restarting = (cfg.wifiSsid != oldSsid) || (newPass.length() > 0);

  String out = String("{\"ok\":true,\"restarting\":") + (restarting ? "true" : "false") + "}";
  request->send(200, "application/json", out);

  if (restarting) g_restartAtMs = millis() + 1500;
}

static void handleAction(AsyncWebServerRequest *request) {
  String what = param(request, "do", "");

  if (what == "restart") {
    request->send(200, "application/json", "{\"ok\":true}");
    g_restartAtMs = millis() + 800;
    return;
  }
  if (what == "factory_reset") {
    logLine("Factory reset asked for from the settings page");
    settingsFactoryReset();
    g_forgetWifi = true;          // clears the chip's copy of the network too
    request->send(200, "application/json", "{\"ok\":true}");
    g_restartAtMs = millis() + 1200;
    return;
  }
  if (what == "wifi_forget") {
    logLine("Forgetting the WiFi network and restarting");
    settingsForgetWifi();
    g_forgetWifi = true;
    request->send(200, "application/json", "{\"ok\":true}");
    g_restartAtMs = millis() + 1200;   // long enough for the reply to get out
    return;
  }
  if (what == "log_clear") {
    logClear();
    request->send(200, "application/json", "{\"ok\":true}");
    return;
  }
  if (what == "wx_refresh") {
    logLine("Weather refresh asked for from the settings page");
    weatherRequestRefresh(false);
    request->send(200, "application/json", "{\"ok\":true}");
    return;
  }
  if (what == "alarm_test") {
    int index = (int)paramLong(request, "idx", 0);
    alarmTest(index);
    request->send(200, "application/json", "{\"ok\":true}");
    return;
  }
  if (what == "sound_test") {
    // Takes the tone and volume from the page rather than from the saved
    // settings, so you can listen to a change before deciding to keep it.
    uint8_t sound  = (uint8_t)constrain(paramLong(request, "sound", cfg.alarmSound), 1, SOUND_CUSTOM);
    uint8_t volume = (uint8_t)constrain(paramLong(request, "vol", cfg.alarmVolume), 0, 100);
    logLine("Hear it: sound " + String(sound) + " at volume " + String(volume));
    String custom = param(request, "custom", String(""));
    alarmPreviewSound(sound, volume, custom.length() ? custom.c_str() : nullptr);
    request->send(200, "application/json", "{\"ok\":true}");
    return;
  }
  request->send(400, "application/json", "{\"ok\":false}");
}

void webBegin() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", SETTINGS_PAGE);
  });

  server.on("/api/log", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain; charset=utf-8", logGetAll());
  });

  server.on("/api/status",   HTTP_GET,  sendStatus);
  server.on("/api/settings", HTTP_GET,  sendSettings);
  server.on("/api/settings", HTTP_POST, handleSave);
  server.on("/api/action",   HTTP_POST, handleAction);

  // Anything else sends the browser back to the settings page.
  server.onNotFound([](AsyncWebServerRequest *request) {
    request->redirect("/");
  });

  server.begin();
}

void webTick() {
  if (g_restartAtMs == 0 || millis() < g_restartAtMs) return;

  if (g_forgetWifi) {
    // The chip keeps its own copy of the network name and password, in a
    // different place from our settings. Clearing only our copy left the chip
    // free to reconnect on its own at the next start up, which is why
    // forgetting the network looked like it did nothing. This wipes the chip's
    // copy as well. It has to happen here rather than in the button handler,
    // because dropping the network first would stop the reply reaching the
    // browser.
    logLine("Clearing the network the chip had stored");
    WiFi.disconnect(true, true);
    delay(300);
  }
  ESP.restart();
}
