// tunes.h
// What the alarm can play.
//
// Everything is a ringtone line in the Nokia format, parsed by rtttl.cpp. That
// was not the first design: the sounds used to be tables of notes written out
// by hand here, and two of the pieces that came out of that were wrong enough
// that they were not recognisable. Ringtone lines are better for three
// reasons. Thousands of them have already been written out and checked by
// other people, so a tune can be added by pasting one line. They are short
// enough to keep dozens in flash for almost nothing. And the settings page can
// take one straight from you, so a tune you want does not have to wait for
// anybody to transcribe it.
//
// The pieces kept here are all either plain patterns, which nobody owns, or
// music old enough to be out of copyright. Ringtones of songs and film themes
// are a different matter, and while nothing stops you pasting one into your own
// clock, they are not shipped with it.

#pragma once
#include <Arduino.h>

// One note. An hz of zero is a rest.
struct Note {
  uint16_t hz;
  uint16_t ms;
};

// The most notes one tune can have. Four bytes each, so this costs 1.3 KB of
// memory and is more than any of the tunes below needs.
#define MAX_TUNE_NOTES 340

// The longest ringtone line the clock will read, your own included.
#define MAX_RTTTL_CHARS 1200

// Nothing is added at the end of a tune: each one carries its own gap, written
// into the ringtone line as trailing rests. Adding a fixed silence on top would
// stretch the pause in patterns that already time their own, and the classic
// alarm clock's 625 millisecond gap is part of what makes it that sound.
#define TUNE_TAIL_MS 0

struct Ringtone {
  const char *label;    // what the settings page calls it
  const char *rtttl;
};

// --- the patterns --------------------------------------------------------

// The alarm clock sound everybody born before about 1990 hears in their sleep:
// two short beeps and a longer gap, over and over. 125 milliseconds of tone,
// 125 of silence, 125 of tone, then 625 of silence. At b=120 a sixteenth note
// is exactly 125 milliseconds, so the whole thing writes itself.
static const char RT_CLASSIC2K[] PROGMEM =
  "Classic2k:d=16,o=7,b=120:c,p,c,p,p,p,p,p";

// The same pattern an octave up. Small speakers are usually louder up here,
// and it is more piercing, which for an alarm clock is rather the point.
static const char RT_CLASSIC4K[] PROGMEM =
  "Classic4k:d=16,o=8,b=120:c,p,c,p,p,p,p,p";

static const char RT_BEEP[] PROGMEM =
  "Beep:d=4,o=7,b=120:e,8p,e,8p,e,2p";

static const char RT_TRILL[] PROGMEM =
  "Trill:d=16,o=6,b=167:c,d,c,d,c,d,c,d,4p,c,d,c,d,c,d,c,d,2p";

static const char RT_CHIME[] PROGMEM =
  "Chime:d=2,o=6,b=133:e,c,1p";

// Up and down the notes of a chord, over and over. It used to be called Siren,
// which it does not sound like.
static const char RT_SIREN[] PROGMEM =
  "Arpeggio:d=8,o=6,b=120:c,e,g,c7,g,e,c,e,g,c7,g,e,1p";

// --- music, all of it long out of copyright ------------------------------

// Beethoven. The whole of the opening section, so it is a good while before it
// comes round again.
static const char RT_ELISE[] PROGMEM =
  "FurElise:d=4,o=6,b=94:16e6,16d#6,16e6,16d#6,16e6,16b5,16d6,16c6,8a5,16p,"
  "16c5,16e5,16a5,8b5,16p,16e5,16g#5,16b5,8c6,16p,16e5,16e6,16d#6,16e6,16d#6,"
  "16e6,16b5,16d6,16c6,8a5,16p,16c5,16e5,16a5,8b5,16p,16e5,16c6,16b5,a5,16p,"
  "8b5,16p,16c6,16d6,16e6,8g5,16f6,16e6,16d6,8f5,16e6,16d6,16c6,8e5,16d6,"
  "16c6,16b5,8e5,16e6,16p,16e6,16e7,16p,16e6,16d#6,16e6,16d#6,16e6,16d#6,"
  "16e6,16d#6,16e6,16d#6,16e6,16d#6,16e6,16b5,16d6,16c6,8a5,16p,16c5,16e5,"
  "16a5,8b5,16p,16e5,16c6,16b5,a.5,1p";

// Beethoven again, the tune from the Ninth. Two phrases.
static const char RT_ODE[] PROGMEM =
  "OdeToJoy:d=4,o=6,b=110:e,e,f,g,g,f,e,d,c,c,d,e,8e.,16d,2d,"
  "e,e,f,g,g,f,e,d,c,c,d,e,8d.,16c,2c,1p";

// Mozart, the Rondo alla Turca. This one did not come from here: it is a
// ringtone line that has been around for years and been played by a great many
// phones, which is rather the point of the format. An earlier attempt to write
// this piece out by hand was not recognisable.
static const char RT_TURKISH[] PROGMEM =
  "Mozart3:d=4,o=5,b=125:16d#,16c#,16c,16c#,8e,8p,16f#,16e,16d#,16e,8g#,8p,"
  "16a,16g#,16g,16g#,16d#6,16c#6,16c6,16c#6,16d#6,16c#6,16c6,16c#6,e6,8c#6,"
  "8e6,32b,32c#6,16d#6,8c#6,8b,8c#6,32b,32c#6,16d#6,8c#6,8b,8c#6,32b,32c#6,"
  "16d#6,8c#6,8b,8a#,g#,16d#,32c#,16c,16c#,8e,8p,16f#,16e,16d#,16e,8g#,8p,"
  "16a,16g#,16g,16g#,16d#6,16c#6,16c6,16c#6,16d#6,16c#6,16c6,16c#6,e6,8c#6,"
  "8e6,32b,32c#6,16d#6,8c#6,8b,8c#6,32b,32c#6,16d#6,8c#6,8b,8c#6,32b,32c#6,"
  "16d#6,8c#6,8b,8a#,g#,8g#,8a,8b,8b,16c#6,16b,16a";

// The order these appear in on the settings page. The classic alarm is first
// because it is the default.
static const Ringtone RINGTONES[] = {
  { "Classic alarm clock (2 kHz)", RT_CLASSIC2K   },
  { "Classic alarm clock (4 kHz)", RT_CLASSIC4K   },
  { "Beep",                        RT_BEEP        },
  { "Trill",                       RT_TRILL       },
  { "Chime",                       RT_CHIME       },
  { "Arpeggio",                    RT_SIREN       },
  { "Fur Elise, Beethoven",        RT_ELISE       },
  { "Ode to Joy, Beethoven",       RT_ODE         },
  { "Turkish March, Mozart",       RT_TURKISH     },
};

#define RINGTONE_COUNT ((int)(sizeof(RINGTONES) / sizeof(Ringtone)))

// Picking this one plays whatever ringtone line you pasted on the settings
// page instead of one of the above.
#define SOUND_CUSTOM 99
