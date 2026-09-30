// weather.h
// Fetches the forecast in the background so the clock never freezes while it
// waits for the internet.
//
// The weather comes from Open-Meteo, which is free and does not need an
// account or an API key. The ZIP code is turned into a latitude and longitude
// once by Zippopotam, which is also free and needs no account.

#pragma once
#include <Arduino.h>
#include "config.h"

struct WeatherHour {
  int8_t  hour24 = -1;
  float   temp   = 0;
  int16_t code   = -1;
};

struct WeatherDay {
  int8_t  weekday = -1;   // 0 is Sunday, 6 is Saturday
  float   high = 0, low = 0;
  int16_t code = -1;
};

struct WeatherData {
  bool     valid     = false;
  bool     isDaytime = true;
  float    temp = 0, feelsLike = 0, high = 0, low = 0, wind = 0;
  int      humidity = 0;
  int16_t  code = -1;
  String   place  = "";
  String   status = "Not fetched yet";
  uint32_t lastUpdateMs = 0;

  // Minutes past midnight, local time. Minus one means not known yet.
  int sunriseMinutes = -1;
  int sunsetMinutes  = -1;

  uint8_t     hourCount = 0;
  WeatherHour hours[FORECAST_HOUR_SLOTS];

  uint8_t     dayCount = 0;
  WeatherDay  days[MAX_FORECAST_DAYS];
};

// Starts the background task. Call once, after WiFi is connected.
void weatherBegin();

// Throws away the readings the clock is holding, so the screens stop showing
// them. Used when the postcode changes, because those readings describe
// somewhere else.
void weatherForget();

// Asks the background task to fetch again as soon as it can.
// Pass true if the ZIP code or country changed, so it looks the place up again.
void weatherRequestRefresh(bool lookUpLocationAgain);

// Asks for a fetch because the user tapped through to a weather screen.
// Does nothing if a fetch is already running or one finished a moment ago, so
// tapping back and forth does not hammer the weather server.
void weatherRequestOnDemand();

// True while a fetch is in progress, so the screen can say so.
bool weatherIsBusy();

// Counts up by one every time a new set of readings lands. The screen watches
// this so it can repaint the instant fresh weather arrives.
uint32_t weatherVersion();

// Copies the current weather into your own struct. Safe to call any time.
void weatherGet(WeatherData &out);

// Plain English description of an Open-Meteo weather code.
const char *weatherText(int code);

// A one word version for tight spaces.
const char *weatherTextShort(int code);
