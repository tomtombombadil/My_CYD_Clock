// alarms.cpp

#include "alarms.h"
#include "tunes.h"
#include "rtttl.h"
#include "logbuf.h"
#include "settings.h"
#include "config.h"

static bool     g_active       = false;
static int      g_activeIndex  = -1;
static uint32_t g_startedMs    = 0;

// What the speaker is doing. A small task of its own reads this; see the note
// above soundTask() for why the sound cannot be run from the main loop.
struct SoundJob {
  bool     playing;
  uint8_t  sound;
  uint8_t  volume;
  uint32_t startedMs;
  uint32_t stopAtMs;    // when to give up; only meaningful if stops is true
  bool     stops;       // the preview plays once, an alarm plays until told
};
static SoundJob g_job = { false, 1, 100, 0, 0, false };

static bool     g_previewing    = false;
static uint32_t g_previewUntilMs = 0;

// The job is written on whichever task asked for a sound and read on the sound
// task, which runs on the other core. Without this lock the reader can see half
// of a new job and half of the old one, and the first version of this went
// wrong in exactly that way: the preview would set its start time and its flag
// separately, the sound task would pick up the flag while the start time was
// still the old one, work out that the sound was long finished, and stop before
// a note ever came out. The alarms were unaffected because they never stop
// themselves, which is why the Test buttons worked while the preview did not.
static portMUX_TYPE g_jobLock = portMUX_INITIALIZER_UNLOCKED;

// Remembers the minute each alarm last went off, so one alarm cannot fire
// over and over for the whole minute it is due.
static int g_lastFiredMinute[ALARM_COUNT] = { -1, -1, -1 };

// The LED pins are wired so that pulling a pin LOW lights that colour.
void ledSet(uint8_t red, uint8_t green, uint8_t blue) {
  digitalWrite(PIN_LED_RED,   red   > 127 ? LOW : HIGH);
  digitalWrite(PIN_LED_GREEN, green > 127 ? LOW : HIGH);
  digitalWrite(PIN_LED_BLUE,  blue  > 127 ? LOW : HIGH);
}

// ---------------------------------------------------------------------------
// The speaker
//
// One pin drives a small amplifier, so the only waveform available is a square
// one: the pin is either at 3.3 volts or at nothing. Two things follow from
// that, and both matter for how this sounds.
//
// Loudness is set by how much of each cycle the pin spends high. Half of it is
// as loud as a square wave gets, and anything either side of a half is quieter,
// so the volume setting can only ever work downwards from where the clock
// already is. The ceiling belongs to the board.
//
// A bare square wave switched straight on and off sounds harsh, and a good part
// of that is not the wave at all but the step change at each end of it, which
// the speaker reports as a click. Every note here is faded in and out over a
// few milliseconds to take those clicks away.
// ---------------------------------------------------------------------------

// Pulse widths are counted out of 1 << TONE_BITS. Ten bits leaves plenty of
// room at every frequency this uses, and gives the volume setting about five
// hundred usable steps.
#define TONE_BITS   10

// Loudness within one note, out of 1000.
#define ENV_FULL    1000

// The note the confirmation beep and the idle timer use.
#define BEEP_HZ  2637

// How long the LED flash pattern takes to come round.
#define LED_CYCLE_MS 2000UL

// Fade at each end of every note, in milliseconds.
#define NOTE_RAMP_MS 10

static int g_toneFreq = 0;    // what the timer is set to now, to avoid resetting it

// Works out the pulse width that will produce a wanted loudness.
//
// The speaker and its amplifier only really pass the fundamental of the square
// wave, and the size of that fundamental follows sin(pi x width), so the width
// needed for a given size is the arc sine of it. The setting is squared first
// because the ear hears loudness on a squashed scale: without that, nearly
// nothing happens over the first three quarters of the slider's travel.
static uint32_t dutyFor(uint8_t volume, uint16_t envelope) {
  if (volume == 0 || envelope == 0) return 0;
  float want = (float)volume / 100.0f;
  want = want * want;
  want *= (float)envelope / (float)ENV_FULL;
  if (want > 1.0f) want = 1.0f;
  float width = asinf(want) / (float)PI;              // 0 to 0.5
  uint32_t ticks = (uint32_t)(width * (float)(1UL << TONE_BITS) + 0.5f);
  if (ticks == 0) ticks = 1;                          // audible, barely
  return ticks;
}

static void toneStop() {
  ledcWrite(PWM_CHANNEL_SPEAKER, 0);
  g_toneFreq = 0;
}

// Holds one note at one loudness. Called over and over while a note plays, so
// that the fade in and out can be drawn out across it.
static void toneSet(int frequency, uint16_t envelope, uint8_t volume) {
  uint32_t duty = dutyFor(volume, envelope);
  if (duty == 0) { toneStop(); return; }
  if (frequency != g_toneFreq) {
    ledcSetup(PWM_CHANNEL_SPEAKER, frequency, TONE_BITS);
    g_toneFreq = frequency;
  }
  ledcWrite(PWM_CHANNEL_SPEAKER, duty);
}

// How loud a note should be this far into itself. Full loudness in the middle,
// fading up and down over `ramp` milliseconds at the ends.
static uint16_t envelopeAt(uint32_t into, uint32_t length, uint32_t ramp) {
  if (into >= length) return 0;
  if (ramp * 2 > length) ramp = length / 2;
  if (ramp == 0) return ENV_FULL;
  if (into < ramp)          return (uint16_t)((uint32_t)ENV_FULL * into / ramp);
  if (into > length - ramp) return (uint16_t)((uint32_t)ENV_FULL * (length - into) / ramp);
  return ENV_FULL;
}

void beepOnce() {
  if (g_job.playing) return;                  // something better is using the speaker
  const uint32_t length = 90;
  uint32_t start = millis(), into;
  while ((into = millis() - start) < length) {
    toneSet(2700, envelopeAt(into, length, 10), cfg.alarmVolume);
    delay(2);
  }
  toneStop();
}

// ---------------------------------------------------------------------------
// The sound task
//
// The main loop comes round about every fifteen milliseconds, and it has to:
// a note's fade in and out lasts twelve, and the trill changes note every
// thirty two. Run from there, the fades would not happen at all and the trill
// would stumble, which is most of what made the old tone sound cheap.
//
// So the speaker gets a small task of its own that wakes every two
// milliseconds. It does nothing but read the job below and set one register,
// and it sleeps the rest of the time.
// ---------------------------------------------------------------------------

// The tune currently loaded, as notes. A ringtone line is read into here once
// when a sound starts rather than being picked apart two hundred times a
// second while it plays.
static Note     g_notes[MAX_TUNE_NOTES];
static uint16_t g_noteCount  = 0;
static uint32_t g_notesMs    = 0;   // how long one pass takes, the tail included

// Guards the three above. Loading a new tune rewrites them, and the sound task
// on the other core may be halfway through reading them for the tune that is
// already playing: pressing Hear it twice, or an alarm going off during a
// preview. Without this it would play a mixture of the two, or hold a note.
// The sound task only ever tries this lock and never waits on it, so it
// simply skips one two millisecond step while a tune is being loaded.
static SemaphoreHandle_t g_tuneLock = nullptr;

// Reads the chosen ringtone into g_notes. Returns how long one pass lasts.
static uint32_t loadTune(uint8_t sound, const char *override = nullptr) {
  char line[MAX_RTTTL_CHARS];
  if (sound == SOUND_CUSTOM) {
    const char *src = (override && *override) ? override : cfg.customRingtone.c_str();
    strncpy(line, src, sizeof(line) - 1);
    line[sizeof(line) - 1] = 0;
  } else {
    int index = (sound >= 1 && sound <= RINGTONE_COUNT) ? (sound - 1) : 0;
    strncpy_P(line, RINGTONES[index].rtttl, sizeof(line) - 1);
    line[sizeof(line) - 1] = 0;
  }

  g_noteCount = rtttlParse(line, g_notes, MAX_TUNE_NOTES);
  if (g_noteCount == 0) {
    // Whatever was pasted in was not a ringtone. Rather than fall silent and
    // leave you wondering, fall back to the plain alarm.
    char fallback[120];
    strncpy_P(fallback, RINGTONES[0].rtttl, sizeof(fallback) - 1);
    fallback[sizeof(fallback) - 1] = 0;
    g_noteCount = rtttlParse(fallback, g_notes, MAX_TUNE_NOTES);
  }

  uint32_t total = TUNE_TAIL_MS;
  for (uint16_t i = 0; i < g_noteCount; i++) total += g_notes[i].ms;
  g_notesMs = total ? total : 1;
  return g_notesMs;
}

// Puts the right note on the speaker for this moment in the tune.
static void soundAt(uint32_t into, uint8_t volume) {
  uint16_t count = g_noteCount;
  uint32_t span  = g_notesMs;
  if (count == 0 || span == 0) { toneStop(); return; }

  uint32_t at = into % span;
  uint32_t startOfNote = 0;
  for (uint16_t i = 0; i < count; i++) {
    uint16_t ms = g_notes[i].ms;
    if (at < startOfNote + ms) {
      uint16_t hz = g_notes[i].hz;
      if (hz == 0) { toneStop(); return; }
      toneSet(hz, envelopeAt(at - startOfNote, ms, NOTE_RAMP_MS), volume);
      return;
    }
    startOfNote += ms;
  }
  toneStop();                     // the quiet tail at the end
}

static void soundTask(void *) {
  for (;;) {
    // The job is read only once the tune lock is held. Reading it first left
    // a gap: a stop could land between the read and the note being set, and
    // the note would then be switched on after the stop had switched it off,
    // with nothing left to switch it off again. A stuck tone. soundStop()
    // takes the same lock, so now the two can never overlap.
    SoundJob job;
    job.playing = false;
    if (xSemaphoreTake(g_tuneLock, 0) == pdTRUE) {
      portENTER_CRITICAL(&g_jobLock);
      job = g_job;
      portEXIT_CRITICAL(&g_jobLock);
    } else {
      vTaskDelay(1);                  // a tune is being loaded; look again soon
      continue;
    }

    if (job.playing) {
      uint32_t now = millis();
      // Signed so that this still reads correctly when the millisecond counter
      // wraps round, which it does about every seven weeks.
      if (job.stops && (int32_t)(now - job.stopAtMs) >= 0) {
        portENTER_CRITICAL(&g_jobLock);
        g_job.playing = false;
        portEXIT_CRITICAL(&g_jobLock);
        toneStop();
      } else {
        soundAt(now - job.startedMs, job.volume);
      }
    }
    xSemaphoreGive(g_tuneLock);
    // Every two milliseconds while something plays, for the fades and the
    // fast notes. When nothing is playing there is no reason to wake five
    // hundred times a second, and twenty milliseconds is still well inside
    // the fade at the start of the first note.
    TickType_t nap = pdMS_TO_TICKS(job.playing ? 2 : 20);
    vTaskDelay(nap ? nap : 1);        // never zero, that would spin the core
  }
}

static void soundStart(uint8_t sound, uint8_t volume, bool once,
                       const char *override = nullptr) {
  // Stop whatever is playing, then wait for the sound task to finish the step
  // it may be in the middle of before the notes are replaced.
  portENTER_CRITICAL(&g_jobLock);
  g_job.playing = false;
  portEXIT_CRITICAL(&g_jobLock);
  xSemaphoreTake(g_tuneLock, portMAX_DELAY);
  uint32_t span = loadTune(sound, override);
  xSemaphoreGive(g_tuneLock);
  uint32_t now  = millis();
  portENTER_CRITICAL(&g_jobLock);
  g_job.sound     = sound;
  g_job.volume    = volume;
  g_job.stops     = once;
  g_job.startedMs = now;
  g_job.stopAtMs  = now + span;
  g_job.playing   = true;
  portEXIT_CRITICAL(&g_jobLock);
  logLine("Sound " + String(sound) + " at volume " + String(volume) +
          ": " + String(g_noteCount) + " notes, " + String(span) + " ms" +
          (once ? ", once" : ""));
}

static void soundStop() {
  xSemaphoreTake(g_tuneLock, portMAX_DELAY);   // waits out the current step
  portENTER_CRITICAL(&g_jobLock);
  g_job.playing = false;
  portEXIT_CRITICAL(&g_jobLock);
  toneStop();
  xSemaphoreGive(g_tuneLock);
}

void alarmsBegin() {
  pinMode(PIN_LED_RED,   OUTPUT);
  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_LED_BLUE,  OUTPUT);
  ledSet(0, 0, 0);

  g_tuneLock = xSemaphoreCreateMutex();
  loadTune(cfg.alarmSound);           // the sound task does not exist yet
  ledcSetup(PWM_CHANNEL_SPEAKER, BEEP_HZ, TONE_BITS);
  ledcAttachPin(PIN_SPEAKER, PWM_CHANNEL_SPEAKER);
  toneStop();

  // Core 0, away from the screen. Priority above the main loop so a note lands
  // when it is meant to rather than whenever the display has finished.
  xTaskCreatePinnedToCore(soundTask, "sound", 3072, nullptr, 3, nullptr, 0);
}

bool alarmIsActive() { return g_active; }

bool alarmActiveUsesScreen() {
  if (!g_active || g_activeIndex < 0) return false;
  return cfg.alarms[g_activeIndex].useScreen;
}

void alarmAcknowledge() {
  if (!g_active) return;
  g_active = false;
  g_activeIndex = -1;
  soundStop();
  ledSet(0, 0, 0);
}

static void startAlarm(int index) {
  g_active      = true;
  g_activeIndex = index;
  g_startedMs   = millis();
  if (cfg.alarms[index].useTone) soundStart(cfg.alarmSound, cfg.alarmVolume, false);
}

void alarmTest(int index) {
  if (index < 0 || index >= ALARM_COUNT) return;
  startAlarm(index);
}

void alarmPreviewSound(uint8_t sound, uint8_t volume, const char *customLine) {
  if (g_active) {
    logLine("Hear it ignored: an alarm is ringing");
    return;
  }
  soundStart(sound, volume > 100 ? 100 : volume, true, customLine);
  // The sound task stops this on its own, but the main loop watches it too.
  // Having one job that stops itself across two cores is what went wrong the
  // first time; this way the loop that is definitely running does it as well.
  g_previewUntilMs = millis() + g_notesMs + 50;
  g_previewing     = true;
}

// Runs the light pattern while an alarm is going off. The sound is not here:
// it belongs to soundTask(), which was started when the alarm was.
static void runEffects() {
  if (g_activeIndex < 0 || g_activeIndex >= ALARM_COUNT) return;
  const AlarmConfig &a = cfg.alarms[g_activeIndex];
  uint32_t inCycle = (millis() - g_startedMs) % LED_CYCLE_MS;

  if (a.useLed) {
    bool on = (inCycle < 400) || (inCycle >= 800 && inCycle < 1200);
    ledSet(on ? 255 : 0, 0, 0);
  }
}

void alarmsTick(struct tm *timeNow, bool timeValid) {
  if (g_previewing && (int32_t)(millis() - g_previewUntilMs) >= 0) {
    g_previewing = false;
    soundStop();
  }

  if (g_active) {
    g_previewing = false;
    if (millis() - g_startedMs > ALARM_GIVE_UP_MS) {
      alarmAcknowledge();
      return;
    }
    runEffects();
    return;
  }

  if (!timeValid) return;

  int minuteOfDay = timeNow->tm_hour * 60 + timeNow->tm_min;
  int weekdayBit  = 1 << timeNow->tm_wday;   // tm_wday is 0 for Sunday

  for (int i = 0; i < ALARM_COUNT; i++) {
    const AlarmConfig &a = cfg.alarms[i];
    if (!a.enabled) continue;
    if ((a.days & weekdayBit) == 0) continue;

    int dueMinute = a.hour * 60 + a.minute;
    if (minuteOfDay != dueMinute) continue;
    if (g_lastFiredMinute[i] == minuteOfDay) continue;

    g_lastFiredMinute[i] = minuteOfDay;
    startAlarm(i);
    return;
  }

  // Once the minute has moved on, allow each alarm to fire again tomorrow.
  for (int i = 0; i < ALARM_COUNT; i++) {
    if (g_lastFiredMinute[i] != -1 && g_lastFiredMinute[i] != minuteOfDay) {
      g_lastFiredMinute[i] = -1;
    }
  }
}
