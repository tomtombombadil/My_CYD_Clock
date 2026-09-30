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
#include <new>

static AsyncWebServer server(80);

// Everything below except the web server object itself belongs to the main
// loop. Nothing in here that runs on the web server's task is allowed to
// change settings, touch the screen, start a sound or write to storage.
static uint32_t g_restartAtMs = 0;
static bool     g_forgetWifi  = false;

// ---------------------------------------------------------------------------
// Handing work from the web server to the main loop
// ---------------------------------------------------------------------------
// The web server runs its requests on a task of its own, which can be on
// either core, while the main loop is drawing on core 1 and the weather task
// is working on core 0. Each request used to change the settings, redraw the
// screen and start sounds itself, right in the middle of whatever the other
// two were doing. A text setting being replaced on one core while another
// core is copying it hands the copier memory that has just been freed, and
// the display library cannot cope with two tasks talking to the screen at
// once. Both of those end in a crash.
//
// So a request now only works out what was asked for. It puts that in a
// mailbox and returns, and the main loop picks it up on its next pass and
// does the work itself, a few milliseconds later.

// A save. The whole new set of settings, built from a copy of the current
// ones with the page's changes laid over it.
struct PendingSave {
  Settings next;
  bool     restarting = false;
};
static PendingSave *g_pendingSave = nullptr;
static portMUX_TYPE g_mailMux     = portMUX_INITIALIZER_UNLOCKED;

// Anything else that has to happen on the main loop.
enum WebCommandType : uint8_t {
  CMD_ALARM_TEST,
  CMD_SOUND_TEST,
  CMD_RESTART,
  CMD_FACTORY_RESET,
  CMD_WIFI_FORGET,
};
struct WebCommand {
  WebCommandType type;
  int      index  = 0;
  uint8_t  sound  = 1;
  uint8_t  volume = 100;
  String   custom;
};
static QueueHandle_t g_commands = nullptr;

// Queues a command for the main loop. False if the queue is full, which only
// happens if someone is pressing buttons faster than the loop comes round.
static bool postCommand(WebCommand *command) {
  if (!g_commands || !command) { delete command; return false; }
  if (xQueueSend(g_commands, &command, 0) != pdTRUE) { delete command; return false; }
  return true;
}

// How long a request may wait for the settings lock. The main loop only holds
// it for a moment, but the watchdog is timing every request, so no request is
// ever allowed to wait without limit.
#define WEB_LOCK_WAIT_MS 500

static void sendBusy(AsyncWebServerRequest *request) {
  request->send(503, "application/json", "{\"ok\":false,\"busy\":true}");
}

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
  // Copy out the two settings this needs, then let go straight away.
  if (!cfgTryLock(WEB_LOCK_WAIT_MS)) { sendBusy(request); return; }
  String ntp = cfg.ntpServer;
  String zip = cfg.zip;
  cfgUnlock();

  DynamicJsonDocument doc(768);

  bool online = (WiFi.status() == WL_CONNECTED);
  doc["online"] = online;
  doc["ssid"]   = online ? WiFi.SSID() : String("");
  doc["rssi"]   = online ? WiFi.RSSI() : 0;
  doc["ip"]     = online ? WiFi.localIP().toString() : String("none");
  doc["host"]   = deviceHostname() + ".local";
  doc["ntp"]    = ntp;

  struct tm now;
  bool haveTime = getLocalTime(&now, 50);
  char timeBuf[40] = "not set yet";
  if (haveTime) strftime(timeBuf, sizeof(timeBuf), "%a %d %b %Y  %H:%M:%S", &now);
  doc["time"]   = timeBuf;
  doc["synced"] = haveTime && (now.tm_year + 1900) > 2020;

  WeatherData wx;
  weatherGet(wx);
  doc["wxok"] = wx.valid;
  doc["wx"]   = (wx.place.length() ? wx.place : zip) + "  " + wx.status;

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
  // Work from a private copy so the lock is held only for the copy itself.
  if (!cfgTryLock(WEB_LOCK_WAIT_MS)) { sendBusy(request); return; }
  Settings *copy = new (std::nothrow) Settings(cfg);
  cfgUnlock();
  if (!copy) { sendBusy(request); return; }
  const Settings &cfg = *copy;         // everything below reads the copy

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
  delete copy;
  request->send(200, "application/json", out);
}


// Runs on the web server's task. Builds the new settings from a copy of the
// current ones and hands them to the main loop. It changes nothing itself.
static void handleSave(AsyncWebServerRequest *request) {
  PendingSave *pending = new (std::nothrow) PendingSave;
  if (!pending) { sendBusy(request); return; }
  if (!cfgTryLock(WEB_LOCK_WAIT_MS)) { delete pending; sendBusy(request); return; }
  pending->next = cfg;
  cfgUnlock();

  Settings &n = pending->next;
  String oldSsid = n.wifiSsid;

  String newSsid = param(request, "ssid", n.wifiSsid);
  String newPass = param(request, "pass", "");
  if (newSsid.length()) n.wifiSsid = newSsid;
  if (newPass.length()) n.wifiPass = newPass;

  n.ntpServer    = param(request, "ntp", n.ntpServer);
  n.timeZone     = param(request, "tz", n.timeZone);
  n.timeZoneName = param(request, "tzname", n.timeZoneName);

  n.use24Hour   = paramBool(request, "h24",   n.use24Hour);
  n.showSeconds = paramBool(request, "secs",  n.showSeconds);
  n.showAmPm    = paramBool(request, "ampm",  n.showAmPm);
  n.blinkColon  = paramBool(request, "blink", n.blinkColon);
  n.showDate    = paramBool(request, "date",  n.showDate);
  n.clockFace     = (uint8_t)paramLong(request, "face", n.clockFace);
  n.ghostSegments = paramBool(request, "ghost", n.ghostSegments);
  n.invertColors  = paramBool(request, "invert", n.invertColors);
  n.segmentStroke = (uint8_t)constrain(paramLong(request, "stroke", n.segmentStroke), 0, 3);

  n.colorText = hexToColor(param(request, "fg", colorToHex(n.colorText)), n.colorText);
  n.colorBack = hexToColor(param(request, "bg", colorToHex(n.colorBack)), n.colorBack);

  n.brightness      = (uint8_t)constrain(paramLong(request, "bri",  n.brightness), 5, 255);
  n.nightBrightness = (uint8_t)constrain(paramLong(request, "nbri", n.nightBrightness), 5, 255);
  n.autoDim         = paramBool(request, "autodim", n.autoDim);

  n.zip     = param(request, "zip", n.zip);
  n.country = param(request, "country", n.country);
  n.metric      = (paramLong(request, "metric", n.metric ? 1 : 0) == 1);
  n.forecastDays = (uint8_t)constrain(paramLong(request, "wxdays", n.forecastDays), 3, MAX_FORECAST_DAYS);
  n.showCurrentScreen = paramBool(request, "wxcur",  n.showCurrentScreen);
  n.showHourlyScreen  = paramBool(request, "wxhr",   n.showHourlyScreen);
  n.showDailyScreen   = paramBool(request, "wxday",  n.showDailyScreen);

  n.alarmSound  = (uint8_t)constrain(paramLong(request, "alsound", n.alarmSound), 1, SOUND_CUSTOM);
  n.alarmVolume = (uint8_t)constrain(paramLong(request, "alvol",   n.alarmVolume), 0, 100);
  n.customRingtone = param(request, "alcustom", n.customRingtone);
  // The clock only ever reads this many characters of a ringtone, and storage
  // silently refuses text much longer than that, so the setting would quietly
  // fail to save.
  if (n.customRingtone.length() >= MAX_RTTTL_CHARS) n.customRingtone.remove(MAX_RTTTL_CHARS - 1);

  for (int i = 0; i < ALARM_COUNT; i++) {
    String prefix = "a" + String(i);
    AlarmConfig &a = n.alarms[i];
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

  if (n.clockFace < FACE_SEVENSEG || n.clockFace > FACE_ITALIC) {
    n.clockFace = FACE_SEVENSEG;
  }

  // Only a change of WiFi network needs a restart.
  pending->restarting = (n.wifiSsid != oldSsid) || (newPass.length() > 0);
  bool restarting = pending->restarting;

  // A second save before the loop picked up the first replaces it. The newer
  // one was built from the same starting point plus everything on the page,
  // so nothing is lost.
  portENTER_CRITICAL(&g_mailMux);
  PendingSave *older = g_pendingSave;
  g_pendingSave = pending;
  portEXIT_CRITICAL(&g_mailMux);
  delete older;

  String out = String("{\"ok\":true,\"restarting\":") + (restarting ? "true" : "false") + "}";
  request->send(200, "application/json", out);
}

// Runs on the main loop. Puts a save from the web page into effect.
static void applySettings(PendingSave &pending) {
  Settings &next = pending.next;

  cfgLock();
  String  oldNtp     = cfg.ntpServer;
  String  oldZone    = cfg.timeZone;
  bool    oldMetric  = cfg.metric;
  uint8_t oldDays    = cfg.forecastDays;

  // A postcode is not a position. The clock looks the position up once and
  // keeps it, so when the postcode changes the position it is holding belongs
  // to the old one and has to go with it. This has to happen before the save,
  // or the stored settings end up holding the new postcode next to the old
  // position, still marked as known, and nothing ever looks the new one up.
  bool placeChanged = (next.zip != cfg.zip || next.country != cfg.country);
  if (placeChanged) {
    next.haveLocation = false;
    next.latitude     = 0.0;
    next.longitude    = 0.0;
    next.placeName    = "";
  } else {
    // The page's copy was taken when the request arrived. The weather task
    // may have finished looking the place up since, so keep what it found
    // rather than putting the older position back.
    next.haveLocation = cfg.haveLocation;
    next.latitude     = cfg.latitude;
    next.longitude    = cfg.longitude;
    next.placeName    = cfg.placeName;
  }
  cfg = next;
  cfgUnlock();

  if (placeChanged) {
    logLine("Postcode changed to " + next.country + " " + next.zip +
            ", forgetting the old position");
  }

  settingsSave(next);                 // from our own copy, outside the lock
  logLine("Settings saved from the web page");

  // Put the new values to work without needing a restart where we can.
  if (next.ntpServer != oldNtp || next.timeZone != oldZone) {
    configTzTime(next.timeZone.c_str(), next.ntpServer.c_str(),
                 "pool.ntp.org", "time.google.com");
  }
  if (placeChanged) {
    // Throw away the readings too. They describe somewhere else, and showing
    // another town's weather under a new postcode is worse than showing none.
    weatherForget();
    weatherRequestRefresh(true);
  } else if (next.metric != oldMetric || next.forecastDays != oldDays) {
    weatherRequestRefresh(false);
  }
  uiSetBrightness(next.brightness);
  uiApplyInvert();
  uiRedraw();

  if (pending.restarting) g_restartAtMs = millis() + 1500;
}

// Runs on the web server's task. Only works out what was asked for.
static void handleAction(AsyncWebServerRequest *request) {
  String what = param(request, "do", "");

  // These two only set a flag or clear the log, both of which are safe from
  // any task, so they happen here and now.
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

  // Everything else goes to the main loop.
  WebCommand *command = new (std::nothrow) WebCommand;
  if (!command) { sendBusy(request); return; }

  if (what == "restart") {
    command->type = CMD_RESTART;
  } else if (what == "factory_reset") {
    command->type = CMD_FACTORY_RESET;
  } else if (what == "wifi_forget") {
    command->type = CMD_WIFI_FORGET;
  } else if (what == "alarm_test") {
    command->type  = CMD_ALARM_TEST;
    command->index = (int)paramLong(request, "idx", 0);
  } else if (what == "sound_test") {
    // Takes the tone and volume from the page rather than from the saved
    // settings, so you can listen to a change before deciding to keep it.
    command->type   = CMD_SOUND_TEST;
    command->sound  = (uint8_t)constrain(paramLong(request, "sound", 1), 1, SOUND_CUSTOM);
    command->volume = (uint8_t)constrain(paramLong(request, "vol", 100), 0, 100);
    command->custom = param(request, "custom", String(""));
    if (command->custom.length() >= MAX_RTTTL_CHARS) command->custom.remove(MAX_RTTTL_CHARS - 1);
  } else {
    delete command;
    request->send(400, "application/json", "{\"ok\":false}");
    return;
  }

  if (!postCommand(command)) { sendBusy(request); return; }
  request->send(200, "application/json", "{\"ok\":true}");
}

// Runs on the main loop.
static void runCommand(WebCommand &command) {
  switch (command.type) {
    case CMD_ALARM_TEST:
      alarmTest(command.index);
      break;

    case CMD_SOUND_TEST:
      logLine("Hear it: sound " + String(command.sound) + " at volume " +
              String(command.volume));
      alarmPreviewSound(command.sound, command.volume,
                        command.custom.length() ? command.custom.c_str() : nullptr);
      break;

    case CMD_RESTART:
      g_restartAtMs = millis() + 800;   // long enough for the reply to get out
      break;

    case CMD_FACTORY_RESET:
      logLine("Factory reset asked for from the settings page");
      settingsFactoryReset();
      g_forgetWifi  = true;             // clears the chip's copy of the network too
      g_restartAtMs = millis() + 1200;
      break;

    case CMD_WIFI_FORGET:
      logLine("Forgetting the WiFi network and restarting");
      settingsForgetWifi();
      g_forgetWifi  = true;
      g_restartAtMs = millis() + 1200;
      break;
  }
}

void webBegin() {
  g_commands = xQueueCreate(6, sizeof(WebCommand *));

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
  // Anything the web page asked for since the last pass.
  PendingSave *pending = nullptr;
  portENTER_CRITICAL(&g_mailMux);
  pending = g_pendingSave;
  g_pendingSave = nullptr;
  portEXIT_CRITICAL(&g_mailMux);
  if (pending) {
    applySettings(*pending);
    delete pending;
  }

  WebCommand *command = nullptr;
  while (g_commands && xQueueReceive(g_commands, &command, 0) == pdTRUE) {
    runCommand(*command);
    delete command;
  }

  if (g_restartAtMs == 0 || (int32_t)(millis() - g_restartAtMs) < 0) return;

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
