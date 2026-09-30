// alarms.h
// Watches the clock for an alarm time and runs whichever of the three
// reminders are switched on: the colour LED on the back, a tone from the
// speaker header, and a flashing screen.

#pragma once
#include <Arduino.h>

void alarmsBegin();

// Call once per pass through the main loop.
void alarmsTick(struct tm *timeNow, bool timeValid);

bool alarmIsActive();

// True when the alarm that is currently going off is set to flash the screen.
bool alarmActiveUsesScreen();

// Stops whatever is currently going off.
void alarmAcknowledge();

// Starts one of the alarms right now, for testing from the settings page.
void alarmTest(int index);

// Turns the colour LED on the back of the board on or off.
// Pass 0 to 255 for each colour.
void ledSet(uint8_t red, uint8_t green, uint8_t blue);

// Plays a short confirmation beep.
void beepOnce();

// Plays one alarm sound once through, so the settings page can let you hear it
// before you save. It does not take over the screen and does not light the LED.
void alarmPreviewSound(uint8_t sound, uint8_t volume,
                       const char *customLine = nullptr);
