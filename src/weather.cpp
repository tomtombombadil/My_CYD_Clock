// weather.cpp
// All of the internet fetching happens on its own background task. The clock
// drawing runs on the main task, so the display keeps ticking even while a
// weather request is waiting for a reply.

#include "weather.h"
#include "settings.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <time.h>
#include "logbuf.h"
#include <esp_heap_caps.h>
#include <lwip/netdb.h>

// The shared copy of the weather. Everything that touches it must hold the
// lock first, because two tasks can reach it at the same time.
static WeatherData  g_weather;
static SemaphoreHandle_t g_lock = nullptr;

static volatile bool     g_wantRefresh = false;
static volatile bool     g_wantGeocode = false;
static volatile bool     g_busy        = false;
static volatile uint32_t g_version     = 0;
static uint32_t          g_lastFetchMs = 0;   // last attempt, good or bad
static uint32_t          g_lastGoodMs  = 0;   // last attempt that actually worked
static uint32_t          g_retryAtMs   = 0;
static uint8_t           g_failures    = 0;

// The settings a fetch needs, copied out of cfg in one go at the start of the
// fetch. The web page can change cfg at any moment, and this task used to read
// it field by field as it went, which is how a postcode could change halfway
// through a lookup. Working from a copy means a fetch always describes one
// place, and it never reads a text setting while another core is replacing it.
struct Where {
  String  zip, country, place;
  double  latitude = 0, longitude = 0;
  bool    haveLocation = false;
  bool    metric = false;
  uint8_t forecastDays = 7;
};

static Where snapshotWhere() {
  Where w;
  cfgLock();
  w.zip          = cfg.zip;
  w.country      = cfg.country;
  w.place        = cfg.placeName;
  w.latitude     = cfg.latitude;
  w.longitude    = cfg.longitude;
  w.haveLocation = cfg.haveLocation;
  w.metric       = cfg.metric;
  w.forecastDays = cfg.forecastDays;
  cfgUnlock();
  return w;
}

const char *weatherText(int code) {
  switch (code) {
    case 0:  return "Clear";
    case 1:  return "Mostly Clear";
    case 2:  return "Partly Cloudy";
    case 3:  return "Cloudy";
    case 45: case 48: return "Fog";
    case 51: case 53: case 55: return "Drizzle";
    case 56: case 57: return "Freezing Drizzle";
    case 61: return "Light Rain";
    case 63: return "Rain";
    case 65: return "Heavy Rain";
    case 66: case 67: return "Freezing Rain";
    case 71: return "Light Snow";
    case 73: return "Snow";
    case 75: return "Heavy Snow";
    case 77: return "Snow Grains";
    case 80: return "Light Showers";
    case 81: return "Showers";
    case 82: return "Heavy Showers";
    case 85: case 86: return "Snow Showers";
    case 95: return "Thunderstorm";
    case 96: case 99: return "Storm with Hail";
    default: return "Unknown";
  }
}

const char *weatherTextShort(int code) {
  switch (code) {
    case 0:  return "Clear";
    case 1:  return "Clear";
    case 2:  return "Partly";
    case 3:  return "Cloudy";
    case 45: case 48: return "Fog";
    case 51: case 53: case 55: case 56: case 57: return "Drizzle";
    case 61: case 63: case 65: case 66: case 67: return "Rain";
    case 71: case 73: case 75: case 77: return "Snow";
    case 80: case 81: case 82: return "Showers";
    case 85: case 86: return "Snow";
    case 95: case 96: case 99: return "Storms";
    default: return "?";
  }
}

// Rounds a temperature to a whole number for the log.
static String tempPlain(float value) { return String((int)lroundf(value)); }

bool     weatherIsBusy()  { return g_busy; }
uint32_t weatherVersion() { return g_version; }

void weatherGet(WeatherData &out) {
  if (!g_lock) return;
  if (xSemaphoreTake(g_lock, pdMS_TO_TICKS(200)) == pdTRUE) {
    out = g_weather;
    xSemaphoreGive(g_lock);
  }
}

static void setStatus(const String &text) {
  if (!g_lock) return;
  if (xSemaphoreTake(g_lock, pdMS_TO_TICKS(200)) == pdTRUE) {
    g_weather.status = text;
    xSemaphoreGive(g_lock);
  }
}

// Pulls the hour and minute out of a timestamp like 2026-09-12T06:45 and
// returns it as minutes past midnight. Returns minus one if it cannot.
static int timeStringToMinutes(const char *iso) {
  if (!iso || strlen(iso) < 16) return -1;
  int hour   = (iso[11] - '0') * 10 + (iso[12] - '0');
  int minute = (iso[14] - '0') * 10 + (iso[15] - '0');
  if (hour < 0 || hour > 23 || minute < 0 || minute > 59) return -1;
  return hour * 60 + minute;
}

// Keeps the opening of the last reply so a failure can show what arrived.
static String g_lastReplyHead;

// The machine name out of a URL, for example api.open-meteo.com.
static String hostFromUrl(const String &url) {
  int start = url.indexOf("://");
  start = (start < 0) ? 0 : start + 3;
  int end = url.indexOf('/', start);
  if (end < 0) end = url.length();
  String host = url.substring(start, end);
  int colon = host.indexOf(':');
  if (colon >= 0) host = host.substring(0, colon);
  return host;
}

// Looks a name up without going through WiFi.hostByName().
//
// hostByName() calls straight into the network stack's name lookup from
// whatever task calls it. The network stack is only safe to call that way from
// its own task, and in this framework build nothing stops two tasks being in
// there at once. The time sync looks its servers up inside the network stack
// on its own schedule, including in the same second or two after start up
// that the first weather fetch does. hostByName() also gives up after fifteen
// seconds but leaves the network stack holding the address of its answer box,
// which is on a stack that has gone by the time a late answer is written
// into it.
//
// lwip_getaddrinfo() asks the network stack's own task to do the lookup and
// waits for it to finish properly, so neither of those can happen.
static bool resolveHost(const String &host, IPAddress &out) {
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family   = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  struct addrinfo *found = nullptr;
  int err = lwip_getaddrinfo(host.c_str(), nullptr, &hints, &found);
  if (err != 0 || !found) return false;
  bool ok = false;
  if (found->ai_family == AF_INET && found->ai_addr) {
    const struct sockaddr_in *sa = (const struct sockaddr_in *)found->ai_addr;
    out = IPAddress((uint32_t)sa->sin_addr.s_addr);
    ok  = ((uint32_t)out != 0);
  }
  lwip_freeaddrinfo(found);
  return ok;
}

// The port in a URL, or the usual one for its scheme.
static uint16_t portFromUrl(const String &url) {
  int start = url.indexOf("://");
  start = (start < 0) ? 0 : start + 3;
  int end = url.indexOf('/', start);
  if (end < 0) end = url.length();
  int colon = url.indexOf(':', start);
  if (colon >= 0 && colon < end) return (uint16_t)url.substring(colon + 1, end).toInt();
  return url.startsWith("https") ? 443 : 80;
}

// Fetches a URL and parses the reply as JSON, keeping only the fields listed
// in the filter. The filter matters: the full forecast reply is bigger than
// the memory we want to spend on it.
//
// The reply is read with getString rather than straight off the socket. The
// weather server sends its answer in pieces with no total length given up
// front, and each piece has a size marker in front of it. getString strips
// those markers out. Handing the raw socket to the JSON reader instead makes
// it read the first size marker as a number and stop there, which looks like
// a successful read of a reply that contains nothing.
static bool fetchJson(const String &url, JsonDocument &doc,
                      const JsonDocument &filter, bool useHttps,
                      const char *label) {
  if (WiFi.status() != WL_CONNECTED) {
    logLine(String(label) + ": skipped, no WiFi");
    return false;
  }

  // Look the name up separately and time it. Doing this here rather than
  // leaving it inside the request means a failure says which stage was slow:
  // finding the server, or waiting for it to answer.
  IPAddress resolved;
  String host = hostFromUrl(url);
  uint32_t lookupStart = millis();
  bool found = resolveHost(host, resolved);
  uint32_t lookupMs = millis() - lookupStart;
  logLine(String(label) + ": found " + host + " at " +
          (found ? resolved.toString() : String("nowhere")) +
          " in " + String(lookupMs) + " ms");

  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(15000);
  http.setReuse(false);

  WiFiClientSecure *secure = nullptr;
  WiFiClient plain;
  bool started = false;

  if (useHttps) {
    secure = new WiFiClientSecure;
    if (!secure) {
      logLine(String(label) + ": not enough memory for a secure connection");
      return false;
    }
    // There is no reliable clock at first boot, so certificate dates cannot
    // be checked. We accept the connection without checking the certificate.
    secure->setInsecure();
    started = http.begin(*secure, url);
  } else {
    // Connect to the address found above, then give the open connection to
    // the HTTP client. Handed a connection that is already open, it uses it
    // as it is, and still names the server properly in the request, so it
    // never does a lookup of its own through hostByName().
    if (!found) {
      logLine(String(label) + ": could not find " + host);
      return false;
    }
    if (!plain.connect(resolved, portFromUrl(url), 8000)) {
      logLine(String(label) + ": could not connect to " + resolved.toString());
      return false;
    }
    plain.setTimeout(15);             // seconds, what the client would have set
    started = http.begin(plain, url);
  }

  if (!started) {
    logLine(String(label) + ": could not open the connection");
    if (secure) delete secure;
    return false;
  }

  uint32_t requestStart = millis();
  int code = http.GET();
  uint32_t requestMs = millis() - requestStart;
  if (code != 200) {
    String why = String(label) + ": server replied " + String(code);
    if (code < 0) why += " (" + HTTPClient::errorToString(code) + ")";
    why += " after " + String(requestMs) + " ms";
    logLine(why);
    http.end();
    if (secure) delete secure;
    return false;
  }

  String body = http.getString();
  http.end();
  if (secure) delete secure;

  g_lastReplyHead = body.substring(0, 140);

  if (body.length() == 0) {
    logLine(String(label) + ": the reply was empty");
    return false;
  }
  logLine(String(label) + ": received " + String(body.length()) + " bytes in " +
          String(requestMs) + " ms");

  DeserializationError err =
      deserializeJson(doc, body, DeserializationOption::Filter(filter));
  if (err) {
    logLine(String(label) + ": could not read the reply, " + String(err.c_str()));
    logLine(String(label) + ": reply began " + g_lastReplyHead);
    return false;
  }
  return true;
}

// Turns the ZIP code into a latitude and longitude. Only needs to run when the
// ZIP code changes, so the result is saved.
//
// The answer is only kept if the postcode is still the one that was looked up.
// If it was changed on the settings page while the lookup was running, the
// answer belongs to the old one, so it is thrown away and the lookup is asked
// for again. Keeping it used to put the old town under the new postcode and
// mark it as known, and because the position is saved, that stuck for good.
static bool lookUpLocation(Where &w) {
  String url = "http://api.zippopotam.us/" + w.country + "/" + w.zip;

  StaticJsonDocument<256> filter;
  filter["places"][0]["latitude"]           = true;
  filter["places"][0]["longitude"]          = true;
  filter["places"][0]["place name"]         = true;
  filter["places"][0]["state abbreviation"] = true;

  logLine("Looking up " + w.country + " " + w.zip);
  DynamicJsonDocument doc(1024);
  if (!fetchJson(url, doc, filter, false, "ZIP lookup")) {
    setStatus("Could not look up ZIP " + w.zip);
    return false;
  }

  JsonArray places = doc["places"].as<JsonArray>();
  if (places.isNull() || places.size() == 0) {
    setStatus("ZIP " + w.zip + " not found");
    return false;
  }

  JsonObject p = places[0];
  double lat = atof(p["latitude"]  | "0");
  double lon = atof(p["longitude"] | "0");

  String name  = p["place name"] | "";
  String state = p["state abbreviation"] | "";
  String place = state.length() ? (name + ", " + state) : name;
  bool   found = (lat != 0.0 || lon != 0.0);

  if (!found) {
    logLine("The ZIP lookup gave no usable position");
    return false;
  }

  cfgLock();
  bool stillCurrent = (cfg.zip == w.zip && cfg.country == w.country);
  if (stillCurrent) {
    cfg.latitude     = lat;
    cfg.longitude    = lon;
    cfg.placeName    = place;
    cfg.haveLocation = true;
  }
  cfgUnlock();

  if (!stillCurrent) {
    logLine("The postcode changed during the lookup, looking up the new one");
    g_wantGeocode = true;
    g_wantRefresh = true;
    return false;
  }

  settingsSaveLocation(lat, lon, place, true);
  w.latitude = lat; w.longitude = lon; w.place = place; w.haveLocation = true;
  logLine("Location is " + place + " at " + String(lat, 4) + ", " + String(lon, 4));
  return true;
}

static bool fetchForecast(const Where &w) {
  if (!w.haveLocation) return false;

  logLine("Forecast for " + (w.place.length() ? w.place : w.zip) +
          " at " + String(w.latitude, 4) + ", " + String(w.longitude, 4));

  String url = "http://api.open-meteo.com/v1/forecast";
  url += "?latitude=";  url += String(w.latitude, 4);
  url += "&longitude="; url += String(w.longitude, 4);
  url += "&current=temperature_2m,relative_humidity_2m,apparent_temperature,"
         "is_day,weather_code,wind_speed_10m";
  url += "&hourly=temperature_2m,weather_code&forecast_hours=24";
  url += "&daily=weather_code,temperature_2m_max,temperature_2m_min,sunrise,sunset";
  url += "&forecast_days="; url += String(w.forecastDays);
  url += "&timezone=auto";
  if (w.metric) {
    url += "&temperature_unit=celsius&wind_speed_unit=kmh";
  } else {
    url += "&temperature_unit=fahrenheit&wind_speed_unit=mph";
  }

  StaticJsonDocument<768> filter;
  JsonObject cur = filter.createNestedObject("current");
  cur["temperature_2m"]       = true;
  cur["relative_humidity_2m"] = true;
  cur["apparent_temperature"] = true;
  cur["is_day"]               = true;
  cur["weather_code"]         = true;
  cur["wind_speed_10m"]       = true;

  JsonObject hr = filter.createNestedObject("hourly");
  hr["time"]           = true;
  hr["temperature_2m"] = true;
  hr["weather_code"]   = true;

  JsonObject dy = filter.createNestedObject("daily");
  dy["time"]               = true;
  dy["weather_code"]       = true;
  dy["temperature_2m_max"] = true;
  dy["temperature_2m_min"] = true;
  dy["sunrise"]            = true;
  dy["sunset"]             = true;

  DynamicJsonDocument doc(12288);
  if (!fetchJson(url, doc, filter, false, "Forecast")) {
    setStatus("Weather server did not answer");
    return false;
  }

  JsonObject current = doc["current"];
  if (current.isNull()) {
    logLine("Forecast: the reply had no current conditions in it");
    logLine("Forecast: reply began " + g_lastReplyHead);
    setStatus("Weather reply was not understood");
    return false;
  }

  // Build the new set of readings in a local copy first, then swap it in.
  WeatherData fresh;
  fresh.valid     = true;
  fresh.temp      = current["temperature_2m"]       | 0.0f;
  fresh.feelsLike = current["apparent_temperature"] | 0.0f;
  fresh.humidity  = current["relative_humidity_2m"] | 0;
  fresh.wind      = current["wind_speed_10m"]       | 0.0f;
  fresh.code      = current["weather_code"]         | -1;
  fresh.isDaytime = ((int)(current["is_day"] | 1) == 1);
  fresh.place     = w.place.length() ? w.place : w.zip;

  // Work out today's date so we can line the daily list up correctly.
  struct tm now;
  bool haveNow = getLocalTime(&now, 50);
  char todayStr[12] = {0};
  if (haveNow) strftime(todayStr, sizeof(todayStr), "%Y-%m-%d", &now);

  // --- Daily forecast ---
  JsonArray dTime = doc["daily"]["time"].as<JsonArray>();
  JsonArray dCode = doc["daily"]["weather_code"].as<JsonArray>();
  JsonArray dMax  = doc["daily"]["temperature_2m_max"].as<JsonArray>();
  JsonArray dMin  = doc["daily"]["temperature_2m_min"].as<JsonArray>();
  JsonArray dRise = doc["daily"]["sunrise"].as<JsonArray>();
  JsonArray dSet  = doc["daily"]["sunset"].as<JsonArray>();

  int todayIndex = 0;
  if (!dTime.isNull() && haveNow) {
    for (size_t i = 0; i < dTime.size(); i++) {
      const char *d = dTime[i] | "";
      if (strncmp(d, todayStr, 10) == 0) { todayIndex = (int)i; break; }
    }
  }

  if (!dRise.isNull() && (int)dRise.size() > todayIndex) {
    fresh.sunriseMinutes = timeStringToMinutes(dRise[todayIndex] | "");
  }
  if (!dSet.isNull() && (int)dSet.size() > todayIndex) {
    fresh.sunsetMinutes = timeStringToMinutes(dSet[todayIndex] | "");
  }

  if (!dCode.isNull()) {
    int startWeekday = haveNow ? now.tm_wday : 0;
    for (size_t i = todayIndex; i < dCode.size() && fresh.dayCount < MAX_FORECAST_DAYS; i++) {
      WeatherDay &d = fresh.days[fresh.dayCount];
      d.code    = dCode[i] | -1;
      d.high    = dMax.isNull() ? 0 : (dMax[i] | 0.0f);
      d.low     = dMin.isNull() ? 0 : (dMin[i] | 0.0f);
      d.weekday = (startWeekday + (int)(i - todayIndex)) % 7;
      fresh.dayCount++;
    }
  }

  if (fresh.dayCount > 0) {
    fresh.high = fresh.days[0].high;
    fresh.low  = fresh.days[0].low;
  }

  // --- Hourly forecast ---
  // The reply may start earlier than right now, so we skip past any hour that
  // has already happened. Comparing the timestamp text works because the
  // format sorts the same way the times do.
  JsonArray hTime = doc["hourly"]["time"].as<JsonArray>();
  JsonArray hTemp = doc["hourly"]["temperature_2m"].as<JsonArray>();
  JsonArray hCode = doc["hourly"]["weather_code"].as<JsonArray>();

  if (!hTime.isNull() && !hTemp.isNull()) {
    char nowStr[20] = {0};
    if (haveNow) strftime(nowStr, sizeof(nowStr), "%Y-%m-%dT%H:00", &now);

    for (size_t i = 0; i < hTime.size() && fresh.hourCount < FORECAST_HOUR_SLOTS; i++) {
      const char *t = hTime[i] | "";
      if (strlen(t) < 16) continue;
      // Skip the hour we are already in as well as any that have gone. The
      // current weather screen covers right now, so this screen starts at the
      // next hour and looks further ahead.
      if (haveNow && strcmp(t, nowStr) <= 0) continue;

      WeatherHour &h = fresh.hours[fresh.hourCount];
      h.hour24 = (t[11] - '0') * 10 + (t[12] - '0');
      h.temp   = hTemp[i] | 0.0f;
      h.code   = hCode.isNull() ? -1 : (hCode[i] | -1);
      fresh.hourCount++;
    }
  }

  fresh.lastUpdateMs = millis();
  fresh.status = "Updated";

  logLine("Weather: " + tempPlain(fresh.temp) + (w.metric ? "C " : "F ") +
          weatherText(fresh.code) + ", " + String(fresh.hourCount) +
          " hours and " + String(fresh.dayCount) + " days received");

  // Same check as the lookup: if the postcode changed while this forecast was
  // on its way, it describes the old place, so it is not shown.
  cfgLock();
  bool stillCurrent = (cfg.zip == w.zip && cfg.country == w.country);
  cfgUnlock();
  if (!stillCurrent) {
    logLine("Forecast: the postcode changed while it was fetched, discarding it");
    return false;
  }

  if (xSemaphoreTake(g_lock, pdMS_TO_TICKS(500)) == pdTRUE) {
    g_weather = fresh;
    xSemaphoreGive(g_lock);
  }
  // Only after the new readings are in place. Counting up first let the
  // screen see the new number, repaint from the old readings, and then have
  // no reason to repaint again.
  g_lastGoodMs = millis();
  g_version++;
  return true;
}

static void weatherTask(void *param) {
  (void)param;
  // One fetch as soon as the clock is on the network, so the weather screens
  // have something on them the first time anyone taps through, and so the
  // sunrise and sunset times are known. Just long enough for the network to
  // settle first.
  vTaskDelay(pdMS_TO_TICKS(1500));
  for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) {
    vTaskDelay(pdMS_TO_TICKS(250));
  }
  logLine("Weather task started");
  g_wantRefresh = true;
  g_wantGeocode = !snapshotWhere().haveLocation;

  for (;;) {
    // A refresh interval of zero means the weather is only fetched when the
    // screen is tapped. The very first fetch after power on always happens,
    // so there is something to show and so sunrise and sunset are known.
    bool haveFetchedOnce = (g_lastFetchMs != 0);

    // Dimming after sunset needs sunrise and sunset times, and those only
    // arrive with the forecast. If that setting is on, fetch once every twelve
    // hours even with background refresh switched off, so the dimming does not
    // drift out of step with the seasons. Twice a day is not pestering anyone.
    bool dimmingNeedsData = cfg.autoDim && g_lastGoodMs != 0 &&
                            (millis() - g_lastGoodMs > 12UL * 3600UL * 1000UL);

    // The weather is only ever fetched for a reason: to fill the screens when
    // the clock starts, because somebody tapped through to a weather screen,
    // or to keep the sunrise and sunset times current for the dimming. It is
    // never fetched on a timer just in case.
    if ((g_wantRefresh || !haveFetchedOnce || dimmingNeedsData) &&
        WiFi.status() == WL_CONNECTED) {
      g_wantRefresh = false;
      g_busy = true;
      g_lastFetchMs = millis();

      Where w = snapshotWhere();
      if (g_wantGeocode || !w.haveLocation) {
        // Cleared before the lookup, not after it. A new postcode saved while
        // the lookup runs sets this again, and clearing it afterwards used to
        // wipe that request out.
        g_wantGeocode = false;
        w.haveLocation = false;
        setStatus("Looking up " + w.zip + "...");
        if (!lookUpLocation(w)) {
          if (!w.haveLocation) g_wantGeocode = true;   // try again next time
        }
      }

      bool ok = false;
      if (w.haveLocation) {
        setStatus("Fetching forecast...");
        ok = fetchForecast(w);
      }
      g_busy = false;

      // These two numbers are what tells us whether a restart was caused by
      // running out of memory or by this task running out of stack space.
      logLine("After the fetch: free memory " + String(ESP.getFreeHeap()) +
              " bytes, largest free block " +
              String(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)) +
              " bytes, weather task had " +
              String((unsigned)uxTaskGetStackHighWaterMark(NULL) * 4) +
              " bytes of stack to spare");

      if (ok) {
        g_failures = 0;
      } else {
        // Try again soon after the first slip and back off from there, rather
        // than leaving the screen empty for a whole minute every time.
        if (g_failures < 250) g_failures++;
        uint32_t wait = 10000UL;
        if (g_failures == 2) wait = 20000UL;
        else if (g_failures == 3) wait = 40000UL;
        else if (g_failures > 3) wait = 60000UL;
        logLine("Weather: attempt " + String(g_failures) + " failed, trying again in " +
                String(wait / 1000UL) + " seconds");
        g_retryAtMs = millis() + wait;
      }
    }

    // Retry after a failure.
    if (g_retryAtMs != 0 && millis() >= g_retryAtMs) {
      g_retryAtMs = 0;
      g_wantRefresh = true;
    }

    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

void weatherBegin() {
  if (!g_lock) g_lock = xSemaphoreCreateMutex();
  g_weather.place = cfg.placeName.length() ? cfg.placeName : cfg.zip;
  // Core 0 is the quieter of the two cores on this chip, so the network work
  // goes there and the screen drawing stays on core 1.
  xTaskCreatePinnedToCore(weatherTask, "weather", 16384, nullptr, 1, nullptr, 0);
}

void weatherForget() {
  if (!g_lock) return;
  if (xSemaphoreTake(g_lock, pdMS_TO_TICKS(300)) == pdTRUE) {
    g_weather = WeatherData();          // back to nothing known
    g_weather.status = "Looking up the new postcode";
    xSemaphoreGive(g_lock);
  }
  g_lastGoodMs = 0;
  g_version++;
}

void weatherRequestOnDemand() {
  if (g_busy) return;

  // Readings less than half a minute old are fresh enough, which stops a flick
  // through the three weather screens firing three requests.
  //
  // Note that this asks when the weather was last *fetched*, not when it was
  // last *tried*. Those are different, and using the wrong one meant a failed
  // attempt locked out tapping for the next half minute, which is exactly when
  // you most want it to try again.
  if (g_lastGoodMs != 0 && millis() - g_lastGoodMs < 30000UL) return;

  // A tap goes to the front of the queue. Whatever the background retry was
  // waiting for, the person is standing in front of the clock now.
  g_retryAtMs = 0;
  g_wantRefresh = true;
}

void weatherRequestRefresh(bool lookUpLocationAgain) {
  // The main loop has already cleared the saved position in cfg when the
  // postcode changed. This only has to tell the task to look it up.
  if (lookUpLocationAgain) g_wantGeocode = true;
  g_wantRefresh = true;
}
