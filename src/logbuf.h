// logbuf.h
// Keeps the last few dozen lines of activity in memory so they can be read
// from the settings page.
//
// A copy of the log also goes into the small pocket of memory that survives a
// restart. If the clock reboots on its own, the lines leading up to it are
// still there afterwards, along with the reason the chip gives for the
// restart. That is the only practical way to work out why something happened
// overnight while nobody was watching.
//
// That pocket of memory is cleared by pulling the power, but it survives a
// crash, a watchdog timeout, and a restart the program asks for itself.

#pragma once
#include <Arduino.h>

#define LOG_LINE_COUNT 50

void   logBegin();
void   logLine(const String &text);
String logGetAll();
void   logClear();

// Call once per pass through the main loop. Keeps a note of how long this run
// has lasted and the lowest the free memory has been, both of which survive a
// restart.
void logTick();

// A one line summary of the restart: which number it is, what the chip says
// caused it, how long the previous run lasted, and the lowest free memory it
// saw. Safe to call at any time.
String logRestartSummary();

// How many times the clock has restarted since it was last unplugged.
uint32_t logRestartCount();
