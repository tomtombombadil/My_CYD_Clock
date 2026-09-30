// logbuf.cpp

#include "logbuf.h"
#include <esp_task_wdt.h>
#include <time.h>
#include <esp_system.h>

// ---------------------------------------------------------------------------
// The part that survives a restart
// ---------------------------------------------------------------------------
// RTC_NOINIT_ATTR puts these in a small area of memory that the chip does not
// wipe when it reboots. The magic number tells us whether what is in there is
// ours or just whatever the memory happened to hold at power on.

#define KEEP_LOG_BYTES 1400
#define KEEP_MAGIC     0xC10CC10CUL

RTC_NOINIT_ATTR static uint32_t keepMagic;
RTC_NOINIT_ATTR static uint32_t keepRestarts;
RTC_NOINIT_ATTR static uint32_t keepLastRunSeconds;
RTC_NOINIT_ATTR static uint32_t keepLowestHeap;
RTC_NOINIT_ATTR static uint16_t keepPos;
RTC_NOINIT_ATTR static char     keepLog[KEEP_LOG_BYTES];

// When the watchdog fires it knows something the reset reason does not: the
// name of the task that was hogging the core and would not let go. That name
// is worth far more than "something stopped responding", so it is written
// here, into the memory that survives the restart, and read back below.
//
// Both cores are recorded, not just core 0. The watchdog watches two things:
// the idle task on core 0, which never gets to run if something on core 0 is
// hogging it, and the web server's task for as long as it is answering a
// request. The web server can be on either core. If it is the one that took
// too long, whatever core 0 happened to be running at that instant is an
// innocent bystander, often the idle task itself, and recording only core 0
// would point the search in the wrong direction.
RTC_NOINIT_ATTR static uint32_t keepStallMagic;
RTC_NOINIT_ATTR static char     keepStallTask[20];    // core 0
RTC_NOINIT_ATTR static char     keepStallTask1[20];   // core 1

#define STALL_MAGIC 0x57415448UL      // changed, so a 1.13.1 record is not misread

static String g_stalledTask  = "";
static String g_stalledTask1 = "";

static void copyTaskName(char *dst, size_t size, TaskHandle_t task) {
  const char *name = task ? pcTaskGetName(task) : nullptr;
  if (!name) name = "unknown";
  size_t i = 0;
  for (; i < size - 1 && name[i]; i++) dst[i] = name[i];
  dst[i] = 0;
}

// The watchdog calls this from inside its interrupt just before it panics.
// Nothing clever is allowed in here: no logging, no allocating, no waiting.
// Copying two short names is about the limit, and that is all this needs.
extern "C" void esp_task_wdt_isr_user_handler(void) {
  copyTaskName(keepStallTask,  sizeof(keepStallTask),  xTaskGetCurrentTaskHandleForCPU(0));
  copyTaskName(keepStallTask1, sizeof(keepStallTask1), xTaskGetCurrentTaskHandleForCPU(1));
  keepStallMagic = STALL_MAGIC;
}

// ---------------------------------------------------------------------------

// The lines themselves, in fixed slots. They used to be Strings, which meant
// every new line freed one block of memory and asked for another of a
// different size, all day long. Fixed slots never touch the heap. They cost
// about 13 KB, set aside once at start up, and are wide enough for the
// longest line this writes, the restart summary.
#define LOG_LINE_CHARS 256
static char g_lines[LOG_LINE_COUNT][LOG_LINE_CHARS];
static int    g_next  = 0;
static int    g_count = 0;
static SemaphoreHandle_t g_lock = nullptr;

static String   g_previousRun   = "";
static String   g_restartReason = "";
static uint32_t g_previousSecs  = 0;
static uint32_t g_previousLow   = 0;

static const char *resetReasonText() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   return "powered on";
    case ESP_RST_EXT:       return "reset pin";
    case ESP_RST_SW:        return "the program asked for a restart";
    case ESP_RST_PANIC:     return "the program crashed";
    case ESP_RST_INT_WDT:   return "interrupt watchdog";
    case ESP_RST_TASK_WDT:  return "task watchdog, something stopped responding";
    case ESP_RST_WDT:       return "watchdog";
    case ESP_RST_DEEPSLEEP: return "woke from sleep";
    case ESP_RST_BROWNOUT:  return "the power supply dipped";
    case ESP_RST_SDIO:      return "sdio";
    default:                return "unknown";
  }
}

void logBegin() {
  if (!g_lock) g_lock = xSemaphoreCreateMutex();

  g_restartReason = resetReasonText();

  if (keepMagic != KEEP_MAGIC) {
    // First run after the power was pulled. Nothing kept is trustworthy.
    keepMagic          = KEEP_MAGIC;
    keepRestarts       = 0;
    keepLastRunSeconds = 0;
    keepLowestHeap     = 0;
    keepPos            = 0;
    keepStallMagic     = 0;
    memset(keepLog, 0, sizeof(keepLog));
  } else {
    keepRestarts++;
    g_previousSecs = keepLastRunSeconds;
    g_previousLow  = keepLowestHeap;

    // Take a copy of what the previous run wrote, oldest part first, then
    // start this run's log with a clean slate.
    g_previousRun.reserve(KEEP_LOG_BYTES + 1);
    for (int i = 0; i < KEEP_LOG_BYTES; i++) {
      char c = keepLog[(keepPos + i) % KEEP_LOG_BYTES];
      if (c >= 9 && c < 127) g_previousRun += c;
    }
    g_previousRun.trim();

    if (keepStallMagic == STALL_MAGIC) {
      keepStallTask[sizeof(keepStallTask) - 1]   = 0;
      keepStallTask1[sizeof(keepStallTask1) - 1] = 0;
      g_stalledTask  = String(keepStallTask);
      g_stalledTask1 = String(keepStallTask1);
    }
  }
  keepStallMagic = 0;

  keepPos            = 0;
  keepLastRunSeconds = 0;
  keepLowestHeap     = ESP.getFreeHeap();
  memset(keepLog, 0, sizeof(keepLog));
}

uint32_t logRestartCount() { return keepRestarts; }

String logRestartSummary() {
  String out = "Restart number " + String(keepRestarts) +
               ", cause: " + g_restartReason;
  if (keepRestarts > 0) {
    out += ". The previous run lasted " + String(g_previousSecs) +
           " seconds and its lowest free memory was " + String(g_previousLow) +
           " bytes";
    if (g_stalledTask.length()) {
      out += ". When the watchdog fired, core 0 was running \"" + g_stalledTask +
             "\" and core 1 was running \"" + g_stalledTask1 + "\"";
    }
  }
  return out;
}

// Writes the time, or the seconds since start up if the time is not known yet.
static void stamp(char *buf, size_t size) {
  struct tm now;
  if (getLocalTime(&now, 5) && (now.tm_year + 1900) > 2020) {
    strftime(buf, size, "%H:%M:%S", &now);
    return;
  }
  snprintf(buf, size, "+%lus", (unsigned long)(millis() / 1000UL));
}

// Copies a line into the memory that survives a restart, oldest bytes being
// overwritten first.
static void keepAppend(const char *entry) {
  for (size_t i = 0; entry[i]; i++) {
    keepLog[keepPos] = entry[i];
    keepPos = (keepPos + 1) % KEEP_LOG_BYTES;
  }
  keepLog[keepPos] = '\n';
  keepPos = (keepPos + 1) % KEEP_LOG_BYTES;
}

void logLine(const String &text) {
  char when[16];
  stamp(when, sizeof(when));
  char entry[LOG_LINE_CHARS];
  snprintf(entry, sizeof(entry), "%s  %s", when, text.c_str());   // long lines are cut short
  Serial.println(entry);

  if (!g_lock) return;
  if (xSemaphoreTake(g_lock, pdMS_TO_TICKS(200)) != pdTRUE) return;

  memcpy(g_lines[g_next], entry, sizeof(entry));
  g_next = (g_next + 1) % LOG_LINE_COUNT;
  if (g_count < LOG_LINE_COUNT) g_count++;

  keepAppend(entry);
  xSemaphoreGive(g_lock);
}

void logTick() {
  keepLastRunSeconds = millis() / 1000UL;
  uint32_t free = ESP.getFreeHeap();
  if (free < keepLowestHeap || keepLowestHeap == 0) keepLowestHeap = free;
}

String logGetAll() {
  String out;
  if (!g_lock) return out;
  if (xSemaphoreTake(g_lock, pdMS_TO_TICKS(300)) != pdTRUE) return out;

  out.reserve(g_count * 70 + g_previousRun.length() + 200);

  if (g_previousRun.length()) {
    out += "----- kept from the run before the last restart -----\n";
    out += g_previousRun;
    out += "\n----- this run -----\n";
  }

  int start = (g_count < LOG_LINE_COUNT) ? 0 : g_next;
  for (int i = 0; i < g_count; i++) {
    out += g_lines[(start + i) % LOG_LINE_COUNT];
    out += '\n';
  }
  xSemaphoreGive(g_lock);

  if (out.length() == 0) out = "Nothing logged yet.\n";
  return out;
}

void logClear() {
  if (!g_lock) return;
  if (xSemaphoreTake(g_lock, pdMS_TO_TICKS(300)) != pdTRUE) return;
  for (int i = 0; i < LOG_LINE_COUNT; i++) g_lines[i][0] = 0;
  g_next  = 0;
  g_count = 0;
  g_previousRun = "";
  xSemaphoreGive(g_lock);
}
