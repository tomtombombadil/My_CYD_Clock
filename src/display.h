// display.h
// Everything that puts pixels on the screen.

#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>

enum ScreenId {
  SCREEN_CLOCK,
  SCREEN_WEATHER_NOW,
  SCREEN_WEATHER_HOURLY,
  SCREEN_WEATHER_DAILY,
  SCREEN_STATUS,
  SCREEN_RESET_CONFIRM,
  SCREEN_ALARM,
  SCREEN_MESSAGE
};

// What, if anything, sits under a given point on the screen right now.
enum TouchTarget {
  TOUCH_NOTHING,
  TOUCH_RESET_BUTTON,
  TOUCH_RESET_ERASE,
  TOUCH_RESET_CANCEL
};

extern TFT_eSPI tft;

void uiBegin();

// Call this often from the main loop. It works out what needs repainting and
// repaints only that.
void uiTick(struct tm *timeNow, bool timeValid);

void     uiSetScreen(ScreenId screen);
ScreenId uiCurrentScreen();

// Moves to the next screen a tap should show, skipping any weather screens
// that are switched off in the settings.
void uiNextScreen();

// Forces the next uiTick to repaint the whole screen. Call this after any
// setting that changes how things look.
void uiRedraw();

// Puts a short message on the screen straight away, used during start up.
// Puts a short message on the screen straight away. The first two lines are
// drawn large and the last two small. Empty lines are skipped and whatever is
// left is centred.
void uiMessage(const String &line1,
               const String &line2 = "",
               const String &line3 = "",
               const String &line4 = "",
               uint16_t color = TFT_WHITE);

// Says what is under a point on the screen, in pixels from the top left.
TouchTarget uiHitTest(int x, int y);

// The screen shown while the setup network is up. `clients` is how many
// devices have joined it, which is used to tell the user where they are up to.
void uiPortalScreen(const String &ssid, const String &address, int clients);

// The screen shown once the clock has joined your network, giving both
// addresses its settings page can be reached at.
void uiConfigureScreen(const String &ipUrl, const String &nameUrl);

// Applies the Invert colours setting. Takes effect straight away.
void    uiApplyInvert();

void    uiSetBrightness(uint8_t level);
uint8_t uiGetBrightness();

// Picks day or night brightness based on sunrise and sunset. minutesNow is
// the current local time as minutes past midnight.
void uiApplyAutoBrightness(int minutesNow);

// Converts a plain 0xRRGGBB colour into the packed format the screen wants.
uint16_t uiColor(uint32_t rgb);
