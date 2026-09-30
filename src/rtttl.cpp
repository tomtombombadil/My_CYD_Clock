// rtttl.cpp
// Reads the Nokia ringtone format and turns it into the same list of notes
// everything else here plays.
//
// The format is one line of text:
//
//     Name:d=4,o=5,b=125:8e6,8d6,f#,8p,g,8p
//
// a name, then the defaults (d is the note length, o the octave, b the speed
// in beats a minute), then the notes. A note is an optional length, the letter,
// an optional sharp, an optional dot to make it half again as long, and an
// optional octave. `p` is a rest.
//
// It is worth supporting because thousands of these have been written out and
// shared over the last twenty five years, so a tune can be added by pasting a
// line of text rather than by someone transcribing it by ear and getting it
// wrong.

#include "rtttl.h"

// One octave of semitones, from C. Octave 4 is the one containing middle C.
static const uint16_t OCTAVE4[12] = {
  262, 277, 294, 311, 330, 349, 370, 392, 415, 440, 466, 494
};

// Which of those twelve each letter is.
static int stepForLetter(char c) {
  switch (c) {
    case 'c': return 0;
    case 'd': return 2;
    case 'e': return 4;
    case 'f': return 5;
    case 'g': return 7;
    case 'a': return 9;
    case 'b': return 11;
    default:  return -1;
  }
}

static uint16_t noteHz(int step, int octave) {
  if (step < 0) return 0;
  if (octave < 4) {                       // shift down from the table
    uint32_t hz = OCTAVE4[step];
    for (int i = octave; i < 4; i++) hz /= 2;
    return (uint16_t)hz;
  }
  uint32_t hz = OCTAVE4[step];
  for (int i = 4; i < octave; i++) hz *= 2;
  return hz > 20000 ? 0 : (uint16_t)hz;    // past hearing, treat as a rest
}

static void skipSpaces(const char *&p) {
  while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
}

static int readNumber(const char *&p) {
  int n = 0;
  bool any = false;
  while (*p >= '0' && *p <= '9') { n = n * 10 + (*p - '0'); p++; any = true; }
  return any ? n : -1;
}

uint16_t rtttlParse(const char *text, Note *out, uint16_t maxNotes, String *nameOut) {
  if (!text || !out || maxNotes == 0) return 0;

  const char *p = text;

  // --- the name, up to the first colon ---
  const char *colon = strchr(p, ':');
  if (!colon) return 0;
  if (nameOut) {
    *nameOut = String(p).substring(0, colon - p);
    nameOut->trim();
  }
  p = colon + 1;

  // --- the defaults, up to the second colon ---
  int defDuration = 4, defOctave = 6, bpm = 63;
  const char *second = strchr(p, ':');
  if (!second) return 0;
  while (p < second) {
    skipSpaces(p);
    char key = *p;
    if (key == 'd' || key == 'o' || key == 'b') {
      p++;
      skipSpaces(p);
      if (*p == '=') p++;
      skipSpaces(p);
      int value = readNumber(p);
      if (value > 0) {
        if      (key == 'd') defDuration = value;
        else if (key == 'o') defOctave   = value;
        else                 bpm         = value;
      }
    } else {
      p++;                                // a comma, or something unexpected
    }
  }
  p = second + 1;

  if (bpm <= 0)         bpm = 63;
  if (defDuration <= 0) defDuration = 4;
  if (defOctave <= 0)   defOctave = 6;

  // A whole note is four beats.
  const uint32_t wholeMs = (uint32_t)(4 * 60000UL) / (uint32_t)bpm;

  uint16_t count = 0;
  while (*p && count < maxNotes) {
    skipSpaces(p);
    if (*p == ',') { p++; continue; }
    if (!*p) break;

    int duration = readNumber(p);
    if (duration <= 0) duration = defDuration;

    skipSpaces(p);
    char letter = *p;
    if (letter >= 'A' && letter <= 'Z') letter += 32;    // accept capitals
    int step = -1;
    bool rest = false;
    if (letter == 'p') { rest = true; p++; }
    else {
      step = stepForLetter(letter);
      if (step < 0) { p++; continue; }                   // junk, skip it
      p++;
      if (*p == '#') { step++; p++; }
      else if (*p == 'b' && step > 0) { step--; p++; }    // a flat, occasionally seen
    }

    bool dotted = false;
    if (*p == '.') { dotted = true; p++; }

    int octave = defOctave;
    skipSpaces(p);
    int explicitOctave = readNumber(p);
    if (explicitOctave > 0) octave = explicitOctave;

    if (*p == '.') { dotted = true; p++; }               // the dot is sometimes last

    uint32_t ms = wholeMs / (uint32_t)duration;
    if (dotted) ms += ms / 2;
    if (ms == 0) ms = 1;
    if (ms > 60000) ms = 60000;

    out[count].hz = rest ? 0 : noteHz(step, octave);
    out[count].ms = (uint16_t)ms;
    count++;
  }
  return count;
}
