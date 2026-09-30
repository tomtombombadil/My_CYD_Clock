// settings.cpp
// Reads and writes the settings using the ESP32 Preferences library, which
// keeps small values in a reserved area of flash memory.
//
// Note on the short key names below: Preferences will not accept a key name
// longer than 15 characters, so the names are abbreviated.

#include "settings.h"
#include <Preferences.h>
#include <WiFi.h>

Settings cfg;

static Preferences prefs;
static const char *NAMESPACE = "cydclock";

// Two separate jobs write settings: the web page when you press Save, and the
// weather task when it works out where your postcode is. They run at the same
// time as each other, and the storage above is one shared thing that can only
// be open once. Without this, one could close it out from under the other part
// way through, and whatever that one was writing would quietly go nowhere.
//
// Every open and close below is wrapped in a claim on this, so the second one
// to arrive waits for the first to finish rather than trampling it.
static SemaphoreHandle_t storageLock = nullptr;

static void claimStorage() {
  if (!storageLock) storageLock = xSemaphoreCreateMutex();
  if (storageLock) xSemaphoreTake(storageLock, portMAX_DELAY);
}

static void releaseStorage() {
  if (storageLock) xSemaphoreGive(storageLock);
}

// The lock around cfg itself. Created before any task starts, the first time
// anything asks for it, which is settingsLoad() during start up.
static SemaphoreHandle_t cfgMutex = nullptr;

static void ensureCfgMutex() {
  if (!cfgMutex) cfgMutex = xSemaphoreCreateMutex();
}

void cfgLock() {
  ensureCfgMutex();
  if (cfgMutex) xSemaphoreTake(cfgMutex, portMAX_DELAY);
}

void cfgUnlock() {
  if (cfgMutex) xSemaphoreGive(cfgMutex);
}

bool cfgTryLock(uint32_t waitMs) {
  ensureCfgMutex();
  if (!cfgMutex) return false;
  return xSemaphoreTake(cfgMutex, pdMS_TO_TICKS(waitMs)) == pdTRUE;
}

// Builds a key name for one field of one alarm, for example "a0h" for the
// hour of the first alarm.
static String alarmKey(int index, const char *field) {
  String k = "a";
  k += index;
  k += field;
  return k;
}

void settingsLoad() {
  ensureCfgMutex();
  claimStorage();
  prefs.begin(NAMESPACE, true);   // true means open read only

  cfg.wifiSsid = prefs.getString("wifissid", cfg.wifiSsid);
  cfg.wifiPass = prefs.getString("wifipass", cfg.wifiPass);

  cfg.ntpServer    = prefs.getString("ntp",    cfg.ntpServer);
  cfg.timeZone     = prefs.getString("tz",     cfg.timeZone);
  cfg.timeZoneName = prefs.getString("tzname", cfg.timeZoneName);

  cfg.zip          = prefs.getString("zip",     cfg.zip);
  cfg.country      = prefs.getString("country", cfg.country);
  cfg.latitude     = prefs.getDouble("lat",     cfg.latitude);
  cfg.longitude    = prefs.getDouble("lon",     cfg.longitude);
  cfg.haveLocation = prefs.getBool("haveloc",   cfg.haveLocation);
  cfg.placeName    = prefs.getString("place",   cfg.placeName);
  cfg.metric       = prefs.getBool("metric",    cfg.metric);
  cfg.showCurrentScreen  = prefs.getBool("wxcur",  cfg.showCurrentScreen);
  cfg.showHourlyScreen   = prefs.getBool("wxhr",   cfg.showHourlyScreen);
  cfg.showDailyScreen    = prefs.getBool("wxday",  cfg.showDailyScreen);
  cfg.forecastDays       = prefs.getUChar("wxdays", cfg.forecastDays);

  cfg.colorText   = prefs.getULong("fg", cfg.colorText);
  cfg.colorBack   = prefs.getULong("bg", cfg.colorBack);
  cfg.clockFace     = prefs.getUChar("face",  cfg.clockFace);
  cfg.ghostSegments = prefs.getBool("ghost",  cfg.ghostSegments);
  cfg.invertColors  = prefs.getBool("invert", cfg.invertColors);
  cfg.segmentStroke = prefs.getUChar("stroke", cfg.segmentStroke);
  cfg.use24Hour   = prefs.getBool("h24",    cfg.use24Hour);
  cfg.showSeconds = prefs.getBool("secs",   cfg.showSeconds);
  cfg.showAmPm    = prefs.getBool("ampm",   cfg.showAmPm);
  cfg.blinkColon  = prefs.getBool("blink",  cfg.blinkColon);
  cfg.showDate    = prefs.getBool("date",   cfg.showDate);

  cfg.brightness      = prefs.getUChar("bri",    cfg.brightness);
  cfg.nightBrightness = prefs.getUChar("nbri",   cfg.nightBrightness);
  cfg.autoDim         = prefs.getBool("autodim", cfg.autoDim);

  cfg.alarmSound  = prefs.getUChar("alsound", cfg.alarmSound);
  cfg.alarmVolume = prefs.getUChar("alvol",   cfg.alarmVolume);
  cfg.customRingtone = prefs.getString("alcustom", cfg.customRingtone);

  for (int i = 0; i < ALARM_COUNT; i++) {
    AlarmConfig &a = cfg.alarms[i];
    a.enabled   = prefs.getBool(alarmKey(i, "en").c_str(), a.enabled);
    a.hour      = prefs.getUChar(alarmKey(i, "h").c_str(),  a.hour);
    a.minute    = prefs.getUChar(alarmKey(i, "m").c_str(),  a.minute);
    a.days      = prefs.getUChar(alarmKey(i, "d").c_str(),  a.days);
    a.useTone   = prefs.getBool(alarmKey(i, "t").c_str(),   a.useTone);
    a.useLed    = prefs.getBool(alarmKey(i, "l").c_str(),   a.useLed);
    a.useScreen = prefs.getBool(alarmKey(i, "s").c_str(),   a.useScreen);
  }

  prefs.end();
  releaseStorage();

  // Guard against stored values that are out of range.
  if (cfg.forecastDays < 3) cfg.forecastDays = 3;
  if (cfg.forecastDays > MAX_FORECAST_DAYS) cfg.forecastDays = MAX_FORECAST_DAYS;
  if (cfg.clockFace < FACE_SEVENSEG || cfg.clockFace > FACE_ITALIC) {
    cfg.clockFace = FACE_SEVENSEG;
  }
  if (cfg.segmentStroke > 3) cfg.segmentStroke = 3;
  if (cfg.brightness < 8) cfg.brightness = 8;
  if (cfg.nightBrightness < 2) cfg.nightBrightness = 2;
}

void settingsSave(const Settings &s) {
  claimStorage();
  prefs.begin(NAMESPACE, false);

  prefs.putString("wifissid", s.wifiSsid);
  prefs.putString("wifipass", s.wifiPass);

  prefs.putString("ntp",    s.ntpServer);
  prefs.putString("tz",     s.timeZone);
  prefs.putString("tzname", s.timeZoneName);

  prefs.putString("zip",     s.zip);
  prefs.putString("country", s.country);
  prefs.putDouble("lat",     s.latitude);
  prefs.putDouble("lon",     s.longitude);
  prefs.putBool("haveloc",   s.haveLocation);
  prefs.putString("place",   s.placeName);
  prefs.putBool("metric",    s.metric);
  prefs.putBool("wxcur",     s.showCurrentScreen);
  prefs.putBool("wxhr",      s.showHourlyScreen);
  prefs.putBool("wxday",     s.showDailyScreen);
  prefs.putUChar("wxdays",   s.forecastDays);

  prefs.putULong("fg",   s.colorText);
  prefs.putULong("bg",   s.colorBack);
  prefs.putUChar("face",  s.clockFace);
  prefs.putBool("ghost",  s.ghostSegments);
  prefs.putBool("invert", s.invertColors);
  prefs.putUChar("stroke", s.segmentStroke);
  prefs.putBool("h24",   s.use24Hour);
  prefs.putBool("secs",  s.showSeconds);
  prefs.putBool("ampm",  s.showAmPm);
  prefs.putBool("blink", s.blinkColon);
  prefs.putBool("date",  s.showDate);

  prefs.putUChar("bri",   s.brightness);
  prefs.putUChar("nbri",  s.nightBrightness);
  prefs.putBool("autodim", s.autoDim);

  prefs.putUChar("alsound", s.alarmSound);
  prefs.putUChar("alvol",   s.alarmVolume);
  prefs.putString("alcustom", s.customRingtone);

  for (int i = 0; i < ALARM_COUNT; i++) {
    const AlarmConfig &a = s.alarms[i];
    prefs.putBool(alarmKey(i, "en").c_str(), a.enabled);
    prefs.putUChar(alarmKey(i, "h").c_str(),  a.hour);
    prefs.putUChar(alarmKey(i, "m").c_str(),  a.minute);
    prefs.putUChar(alarmKey(i, "d").c_str(),  a.days);
    prefs.putBool(alarmKey(i, "t").c_str(),   a.useTone);
    prefs.putBool(alarmKey(i, "l").c_str(),   a.useLed);
    prefs.putBool(alarmKey(i, "s").c_str(),   a.useScreen);
  }

  prefs.end();
  releaseStorage();
}

void settingsSaveLocation(double latitude, double longitude,
                          const String &placeName, bool haveLocation) {
  claimStorage();
  prefs.begin(NAMESPACE, false);
  prefs.putDouble("lat",   latitude);
  prefs.putDouble("lon",   longitude);
  prefs.putBool("haveloc", haveLocation);
  prefs.putString("place", placeName);
  prefs.end();
  releaseStorage();
}

String deviceHostname() {
  String mac = WiFi.macAddress();            // for example 70:4B:CA:8C:43:6C
  String tail = mac.length() >= 2 ? mac.substring(mac.length() - 2) : String("00");
  tail.toLowerCase();
  return String(CLOCK_HOSTNAME_PREFIX) + "_" + tail;
}

void settingsFactoryReset() {
  claimStorage();
  prefs.begin(NAMESPACE, false);
  prefs.clear();
  prefs.end();
  releaseStorage();
}

// Called on the main loop, the only task that changes cfg as a whole.
void settingsForgetWifi() {
  cfgLock();
  cfg.wifiSsid = "";
  cfg.wifiPass = "";
  cfgUnlock();
  claimStorage();
  prefs.begin(NAMESPACE, false);
  prefs.putString("wifissid", "");
  prefs.putString("wifipass", "");
  prefs.end();
  releaseStorage();
}
