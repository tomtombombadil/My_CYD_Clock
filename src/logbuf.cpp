// logbuf.cpp

#include "logbuf.h"
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

// ---------------------------------------------------------------------------

static String g_lines[LOG_LINE_COUNT];
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
  }

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
  }
  return out;
}

static String stamp() {
  struct tm now;
  if (getLocalTime(&now, 5) && (now.tm_year + 1900) > 2020) {
    char buf[12];
    strftime(buf, sizeof(buf), "%H:%M:%S", &now);
    return String(buf);
  }
  char buf[12];
  snprintf(buf, sizeof(buf), "+%lus", (unsigned long)(millis() / 1000UL));
  return String(buf);
}

// Copies a line into the memory that survives a restart, oldest bytes being
// overwritten first.
static void keepAppend(const String &entry) {
  for (unsigned int i = 0; i < entry.length(); i++) {
    keepLog[keepPos] = entry[i];
    keepPos = (keepPos + 1) % KEEP_LOG_BYTES;
  }
  keepLog[keepPos] = '\n';
  keepPos = (keepPos + 1) % KEEP_LOG_BYTES;
}

void logLine(const String &text) {
  String entry = stamp() + "  " + text;
  Serial.println(entry);

  if (!g_lock) return;
  if (xSemaphoreTake(g_lock, pdMS_TO_TICKS(200)) != pdTRUE) return;

  g_lines[g_next] = entry;
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
  for (int i = 0; i < LOG_LINE_COUNT; i++) g_lines[i] = "";
  g_next  = 0;
  g_count = 0;
  g_previousRun = "";
  xSemaphoreGive(g_lock);
}
