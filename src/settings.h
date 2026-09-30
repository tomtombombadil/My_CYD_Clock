// settings.h
// Everything the user can change, and the functions that read and write those
// values to the ESP32's small built in storage area so they survive a reboot.

#pragma once
#include <Arduino.h>
#include "config.h"
#include "tunes.h"

#define ALARM_COUNT 3

// The clock faces you can pick from on the settings page.
// The seven segment face is drawn by the program itself so it can be any size.
// The rest are proper typefaces that come with the display library.
enum ClockFace {
  FACE_SEVENSEG = 1,
  FACE_SANS     = 2,
  FACE_SERIF    = 3,
  FACE_MONO     = 4,
  FACE_ITALIC   = 5
};

struct AlarmConfig {
  bool    enabled   = false;
  uint8_t hour      = 7;
  uint8_t minute    = 0;
  // One bit per day of the week. Bit 0 is Sunday, bit 6 is Saturday.
  // A value of 127 means every day.
  uint8_t days      = 127;
  bool    useTone   = true;
  bool    useLed    = true;
  bool    useScreen = true;
};

struct Settings {
  // --- WiFi ---
  String   wifiSsid = "";
  String   wifiPass = "";

  // --- Time ---
  String   ntpServer = "time.nist.gov";
  // Time zone in the format the ESP32 expects. The settings page has a list
  // of the common ones so you never have to type this by hand.
  String   timeZone  = "EST5EDT,M3.2.0,M11.1.0";
  String   timeZoneName = "US Eastern";

  // --- Location and weather ---
  String   zip        = "15213";   // Oakland, Pittsburgh
  String   country    = "us";
  double   latitude   = 0.0;
  double   longitude  = 0.0;
  bool     haveLocation = false;
  String   placeName  = "";
  bool     metric     = false;   // false gives Fahrenheit and miles per hour
  bool     showCurrentScreen = true;
  bool     showHourlyScreen  = true;
  bool     showDailyScreen   = true;
  uint8_t  forecastDays      = 7;

  // --- Clock look ---
  uint32_t colorText = 0xFF0000;   // stored as plain red green blue, 0xRRGGBB
  uint32_t colorBack = 0x000000;
  uint8_t  clockFace   = FACE_SEVENSEG;
  // Paints the bars that are not lit in a dim version of the text colour, the
  // way a real LED display shows its unlit segments.
  bool     ghostSegments = false;
  // Width in pixels of the dark line drawn between neighbouring segments.
  // Zero runs them together into one solid shape.
  uint8_t  segmentStroke = 2;
  // Some panels show every colour as its opposite: ask for red on black and
  // you get cyan on white. This turns that round. It starts out at whatever
  // suits the board this was built for, and can be changed at any time.
  bool     invertColors = BOARD_DEFAULT_INVERT;
  bool     use24Hour   = false;
  bool     showSeconds = false;
  bool     showAmPm    = true;
  bool     blinkColon  = false;
  bool     showDate    = true;

  // --- Brightness ---
  uint8_t  brightness      = 150;  // 0 to 255
  uint8_t  nightBrightness = 10;
  bool     autoDim         = false;

  // --- Sound ---
  // Which ringtone, as a position in the list in tunes.h, or SOUND_CUSTOM to
  // play whatever line has been pasted into customRingtone.
  uint8_t  alarmSound  = 1;            // the classic alarm clock
  String   customRingtone = "";
  // 0 is silent, 100 is as loud as the board can manage.
  uint8_t  alarmVolume = 100;

  AlarmConfig alarms[ALARM_COUNT];
};

// The one and only copy of the settings, shared by every part of the program.
extern Settings cfg;

void settingsLoad();
void settingsSave();
void settingsSaveLocation();   // saves only the looked up latitude, longitude and place name
void settingsForgetWifi();

// Throws away every stored setting. The clock then starts up with its
// built in defaults, exactly as it did the first time it was flashed.
void settingsFactoryReset();

// What this clock calls itself on the network, for example my_cyd_clock_6c.
// The last two characters come from the board's own hardware address, so two
// of these on one network do not end up with the same name.
String deviceHostname();
