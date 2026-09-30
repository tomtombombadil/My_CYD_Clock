// config.h
// Fixed values used across the whole program. Nothing here changes while the
// clock is running.
//
// Anything that is different from one board to another lives in boards.h
// instead, which this file pulls in.

#pragma once
#include <Arduino.h>
#include "boards.h"

#define FW_NAME      "My CYD Clock"
#define FW_VERSION   "1.13.2"

// Hardware PWM channels.
// Channels 0 and 1 share a timer, and channels 2 and 3 share another one. The
// backlight and the speaker need different frequencies, so they must not share
// a timer. Channel 0 and channel 2 are on different timers.
#define PWM_CHANNEL_BACKLIGHT   0
#define PWM_CHANNEL_SPEAKER     2

// Name of the open network the clock creates when it has no WiFi yet.
#define SETUP_AP_NAME       "My_CYD_Clock"

// What the clock calls itself on your network. The last two characters of the
// board's own hardware address get added on the end, so two of these on one
// network do not collide and each keeps the same name for good.
#define CLOCK_HOSTNAME_PREFIX  "my_cyd_clock"

// How long you have to hold a finger on the screen for it to count as a long
// press instead of a tap, in milliseconds.
#define LONG_PRESS_MS       700

// An alarm gives up and switches itself off after this long, in milliseconds.
#define ALARM_GIVE_UP_MS    (5UL * 60UL * 1000UL)

// Weather and status screens return to the clock on their own after this long
// with no touch, in milliseconds.
#define SCREEN_RETURN_MS    60000UL
#define STATUS_RETURN_MS    25000UL

// Largest number of forecast days the settings page will let you pick.
#define MAX_FORECAST_DAYS   10

// How many hours the hourly forecast screen shows, starting with the next
// hour. The current hour is not shown: the current weather screen covers it.
#define FORECAST_HOUR_SLOTS 4
