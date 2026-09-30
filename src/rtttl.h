// rtttl.h
// Turning a line of Nokia ringtone text into notes this clock can play.

#pragma once
#include <Arduino.h>
#include "tunes.h"

// Reads one RTTTL line into `out`, stopping at `maxNotes`. Returns how many
// notes it produced, or zero if the text was not a ringtone at all. The tune's
// name goes into `nameOut` when one is given.
//
// It is forgiving on purpose: capital letters, spaces in odd places, flats
// written as `b`, and a dot before or after the octave are all accepted,
// because ringtone lines copied off the web are written every which way.
uint16_t rtttlParse(const char *text, Note *out, uint16_t maxNotes,
                    String *nameOut = nullptr);
