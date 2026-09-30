// display.cpp
// All of the drawing lives here.
//
// The big time is not drawn with a stock font. The clock draws each seven
// segment numeral itself out of filled shapes, so a numeral can be any size
// rather than being stuck at whatever sizes a fixed font happens to offer.
// That is what lets a 12 hour time, which has one numeral fewer, grow to fill
// the screen.
//
// The other clock faces use the proper typefaces that ship with the display
// library. The program tries them from largest to smallest and takes the first
// one that fits.
//
// Nothing is painted twice if it has not changed. Each numeral sits in its own
// box and only boxes whose contents changed get repainted, which is what keeps
// the display from flickering.

#include "display.h"
#include "settings.h"
#include "weather.h"
#include "alarms.h"
#include <WiFi.h>
#include <time.h>


TFT_eSPI tft = TFT_eSPI();

static ScreenId g_screen          = SCREEN_MESSAGE;
static bool     g_forceFull       = true;
static uint8_t  g_brightness      = 200;
static uint32_t g_screenEnteredMs = 0;

uint16_t uiColor(uint32_t rgb) {
  return tft.color565((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

// ---------------------------------------------------------------------------
// Fitting the layout to whatever screen this board has
// ---------------------------------------------------------------------------
// Two screen sizes are in play. The 2.8 and 3.2 inch boards are 320 across by
// 240 down. The 3.5 inch board is 480 by 320, half as big again.
//
// Rather than typing in pixel positions, which would leave everything huddled
// in one corner of the larger screen, the screens below work in fractions of
// whatever screen they find themselves on. So "a third of the way down" is
// written as acrossY(0.33) and comes out right on either board.

// A position a given fraction of the way across, or down, the screen.
static inline int acrossX(float f) { return (int)(tft.width()  * f + 0.5f); }
static inline int downY(float f)   { return (int)(tft.height() * f + 0.5f); }

// True on the larger board, where the smaller typefaces would look lost.
static bool bigScreen() { return tft.width() >= 400; }

// The bold typeface used for headings, the AM or PM marker and the date.
static const GFXfont *fontLabel() {
  return bigScreen() ? &FreeSansBold18pt7b : &FreeSansBold12pt7b;
}

// A step down from that, for tighter spots like the rows of a forecast.
static const GFXfont *fontLabelSmall() {
  return bigScreen() ? &FreeSansBold12pt7b : &FreeSansBold9pt7b;
}

// The plain typeface used for ordinary lines of text. These are the display
// library's own built in faces, picked by number: 2 is small, 4 is larger.
static uint8_t fontBody() { return bigScreen() ? 4 : 2; }

// How tall a line of that plain text is, including the gap below it.
static int bodyLineHeight() { return bigScreen() ? 26 : 18; }

// Selects the plain typeface and reports how tall a line of it is.
static int useBodyFont() {
  tft.setTextFont(fontBody());
  tft.setTextSize(1);
  return bodyLineHeight();
}

// The typefaces this program uses, largest first. Anything that has to fit a
// given width walks down this until it does.
#define FONT_RUNGS 5

static void useRung(int rung) {
  tft.setTextSize(1);
  switch (rung) {
    case 0:  tft.setFreeFont(fontLabel());      break;
    case 1:  tft.setFreeFont(fontLabelSmall()); break;
    case 2:  tft.setTextFont(fontBody());       break;
    case 3:  tft.setTextFont(2);                break;
    default: tft.setTextFont(1);                break;
  }
}

// Picks the largest typeface, starting no bigger than `from`, whose rendering
// of this text fits the width given. Returns how tall a line of it is.
//
// Every line that has to sit across the screen goes through this. Choosing a
// size by eye and hoping is what let a long message run off both edges at once
// on the wider board, where the plain typeface is a size up and the same
// sentence needs half as much room again.
static int useFontThatFits(const String &text, int widest, int from) {
  for (int rung = from; rung < FONT_RUNGS; rung++) {
    useRung(rung);
    if (tft.textWidth(text) <= widest || rung == FONT_RUNGS - 1) {
      return tft.fontHeight();
    }
  }
  return tft.fontHeight();
}

// ---------------------------------------------------------------------------
// Backlight
// ---------------------------------------------------------------------------

void uiSetBrightness(uint8_t level) {
  if (level < 5) level = 5;          // never go fully dark, it looks broken
  g_brightness = level;
  ledcWrite(PWM_CHANNEL_BACKLIGHT, level);
}

uint8_t uiGetBrightness() { return g_brightness; }

void uiApplyAutoBrightness(int minutesNow) {
  if (!cfg.autoDim) { uiSetBrightness(cfg.brightness); return; }

  WeatherData wx;
  weatherGet(wx);
  if (wx.sunriseMinutes < 0 || wx.sunsetMinutes < 0 || minutesNow < 0) {
    uiSetBrightness(cfg.brightness);
    return;
  }
  bool daytime = (minutesNow >= wx.sunriseMinutes && minutesNow < wx.sunsetMinutes);
  uiSetBrightness(daytime ? cfg.brightness : cfg.nightBrightness);
}

// ---------------------------------------------------------------------------
// Shape helpers
// ---------------------------------------------------------------------------

// Fills a shape that has no dents in it, given its corners in order.
static void fillConvex(const int16_t *px, const int16_t *py, int count, uint16_t color) {
  for (int i = 1; i + 1 < count; i++) {
    tft.fillTriangle(px[0], py[0], px[i], py[i], px[i + 1], py[i + 1], color);
  }
}

// Which bars are lit for each numeral.
// Bit order: top, top right, bottom right, bottom, bottom left, top left, middle.
static const uint8_t SEGMENT_MAP[10] = {
  0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
};

// One bar, described so that the fill and the stroke can both work from it.
// For a bar lying flat, x and y are the left end of the band it sits in and
// len runs to the right. For a bar standing up, len runs downwards.
struct SegBar { bool across; int x, y, len, thick; };

// Works out the six corners of a bar. `pull` shortens it at both ends without
// making it any thinner.
//
// Every corner is a whole number, and both ends are worked out from the same
// centre line, so the shape is the same at one end as at the other. Working in
// fractions of a pixel here is what made earlier versions come out a pixel
// lopsided.
static bool segmentCorners(const SegBar &s, int pull, int16_t *px, int16_t *py) {
  int t = s.thick, hf = t / 2;

  if (s.across) {
    int x0 = s.x + pull, x1 = s.x + s.len - pull, yc = s.y + hf;
    if (x1 - x0 <= t) return false;
    px[0] = x0;      py[0] = yc;
    px[1] = x0 + hf; py[1] = yc - hf;
    px[2] = x1 - hf; py[2] = yc - hf;
    px[3] = x1;      py[3] = yc;
    px[4] = x1 - hf; py[4] = yc + hf;
    px[5] = x0 + hf; py[5] = yc + hf;
  } else {
    int y0 = s.y + pull, y1 = s.y + s.len - pull, xc = s.x + hf;
    if (y1 - y0 <= t) return false;
    px[0] = xc;      py[0] = y0;
    px[1] = xc + hf; py[1] = y0 + hf;
    px[2] = xc + hf; py[2] = y1 - hf;
    px[3] = xc;      py[3] = y1;
    px[4] = xc - hf; py[4] = y1 - hf;
    px[5] = xc - hf; py[5] = y0 + hf;
  }
  return true;
}

// Traces the angled ends of a bar in the background colour.
//
// Only the angled parts get traced, because those are the only places one bar
// meets another. The two long flat sides face either the background or the
// hole in the middle of the numeral, so tracing them would do nothing except
// make the bar thinner, which is exactly what went wrong at the wider
// settings. Leaving them alone means the bars keep their full weight however
// wide the division is set.
static void strokeSegmentEnds(const SegBar &bar, uint16_t back, uint8_t stroke) {
  if (stroke == 0) return;
  // A line drawn at 45 degrees only covers about seven tenths of a pixel
  // measured across itself, so the ends are traced several times, each pass
  // pulled a little further in, to build up the width that was asked for.
  int passes = (int)lroundf(stroke * 1.4142f);
  if (passes < 1) passes = 1;

  int16_t px[6], py[6];
  for (int pull = 0; pull < passes; pull++) {
    if (!segmentCorners(bar, pull, px, py)) return;
    tft.drawLine(px[0], py[0], px[1], py[1], back);   // the four angled ends
    tft.drawLine(px[2], py[2], px[3], py[3], back);
    tft.drawLine(px[3], py[3], px[4], py[4], back);
    tft.drawLine(px[5], py[5], px[0], py[0], back);
  }
}

// Draws one numeral inside a box.
//
// The seven bars tile exactly: every pair of neighbours shares an edge, and
// the four outer corners come out chamfered at 45 degrees, the same as the
// layout printed on a real display. Bars that are not lit get painted too, so
// the box is always fully repainted and nothing from the previous numeral can
// show through.
static void drawSevenSegment(int x, int y, int w, int h, int t, char ch,
                             uint16_t lit, uint16_t unlit, uint16_t back,
                             uint8_t stroke, bool showUnlit) {
  int hf  = t / 2;
  int mid = h / 2;

  const SegBar bars[7] = {
    { true,  x + hf,    y,                 w - t,    t },   // top
    { false, x + w - t, y + hf,            mid - hf, t },   // top right
    { false, x + w - t, y + mid,           mid - hf, t },   // bottom right
    { true,  x + hf,    y + h - t,         w - t,    t },   // bottom
    { false, x,         y + mid,           mid - hf, t },   // bottom left
    { false, x,         y + hf,            mid - hf, t },   // top left
    { true,  x + hf,    y + (h - t) / 2,   w - t,    t },   // middle
  };
  static const uint8_t bitFor[7] = { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40 };

  uint8_t on = 0;
  if (ch >= '0' && ch <= '9') on = SEGMENT_MAP[ch - '0'];

  int16_t px[6], py[6];

  // The four rounds below have to happen in this order.
  //
  //   1. fill the bars that are not lit
  //   2. trace their outlines, but only if unlit bars are being shown at all
  //   3. fill the bars that are lit, which covers any of those outlines that
  //      strayed onto a lit bar
  //   4. trace the outlines of the lit bars
  //
  // The important part is that a traced end never survives on top of a lit bar
  // unless that bar drew it itself. Two neighbouring bars share an edge, so
  // each of them draws over the same line and one clean dark division appears
  // between them, but a bar that shows nothing can no longer cut a line across
  // a lit one that happens to pass underneath it.
  for (int round = 0; round < 4; round++) {
    bool wantLit    = (round >= 2);
    bool tracing    = (round == 1 || round == 3);
    if (round == 1 && !showUnlit) continue;

    for (int i = 0; i < 7; i++) {
      if ((((on & bitFor[i]) != 0)) != wantLit) continue;

      if (!tracing) {
        if (!segmentCorners(bars[i], 0, px, py)) continue;
        fillConvex(px, py, 6, wantLit ? lit : unlit);
        continue;
      }
      strokeSegmentEnds(bars[i], back, stroke);
    }
  }
}

// A 1 is nothing but the two bars down the right hand side. It is given a box
// exactly wide enough to hold them, so there is no point laying out the other
// five and then suppressing them. Nothing else is ever shown in this box, so
// there is nothing behind them to clear either.
static void drawNumeralOne(int x, int y, int w, int h, int t,
                           uint16_t lit, uint16_t back, uint8_t stroke) {
  int hf = t / 2, mid = h / 2;
  const SegBar bars[2] = {
    { false, x + w - t, y + hf,  mid - hf, t },   // top right
    { false, x + w - t, y + mid, mid - hf, t },   // bottom right
  };
  int16_t px[6], py[6];

  for (int i = 0; i < 2; i++) {
    if (segmentCorners(bars[i], 0, px, py)) fillConvex(px, py, 6, lit);
  }
  for (int i = 0; i < 2; i++) strokeSegmentEnds(bars[i], back, stroke);
}

static void drawSegmentColon(int x, int y, int w, int h, uint16_t color, uint16_t back) {
  int r = h * 7 / 100;
  if (r < 2) r = 2;
  tft.fillRect(x, y, w, h, back);
  tft.fillSmoothCircle(x + w / 2, y + h * 30 / 100, r, color, back);
  tft.fillSmoothCircle(x + w / 2, y + h * 70 / 100, r, color, back);
}

// ---------------------------------------------------------------------------
// The clock faces
// ---------------------------------------------------------------------------

struct FaceStep { const GFXfont *font; uint8_t size; };

// Each list runs from smallest to largest. The program walks it backwards and
// takes the first entry that fits the space available.
static const FaceStep SANS_STEPS[] = {
  { &FreeSansBold12pt7b, 1 }, { &FreeSansBold18pt7b, 1 }, { &FreeSansBold24pt7b, 1 },
  { &FreeSansBold18pt7b, 2 }, { &FreeSansBold24pt7b, 2 }, { &FreeSansBold24pt7b, 3 },
};
static const FaceStep SERIF_STEPS[] = {
  { &FreeSerifBold12pt7b, 1 }, { &FreeSerifBold18pt7b, 1 }, { &FreeSerifBold24pt7b, 1 },
  { &FreeSerifBold18pt7b, 2 }, { &FreeSerifBold24pt7b, 2 }, { &FreeSerifBold24pt7b, 3 },
};
static const FaceStep MONO_STEPS[] = {
  { &FreeMonoBold12pt7b, 1 }, { &FreeMonoBold18pt7b, 1 }, { &FreeMonoBold24pt7b, 1 },
  { &FreeMonoBold18pt7b, 2 }, { &FreeMonoBold24pt7b, 2 },
};
static const FaceStep ITALIC_STEPS[] = {
  { &FreeSansBoldOblique12pt7b, 1 }, { &FreeSansBoldOblique18pt7b, 1 },
  { &FreeSansBoldOblique24pt7b, 1 }, { &FreeSansBoldOblique18pt7b, 2 },
  { &FreeSansBoldOblique24pt7b, 2 }, { &FreeSansBoldOblique24pt7b, 3 },
};

static const FaceStep *faceSteps(uint8_t face, int &count) {
  switch (face) {
    case FACE_SERIF:  count = sizeof(SERIF_STEPS)  / sizeof(FaceStep); return SERIF_STEPS;
    case FACE_MONO:   count = sizeof(MONO_STEPS)   / sizeof(FaceStep); return MONO_STEPS;
    case FACE_ITALIC: count = sizeof(ITALIC_STEPS) / sizeof(FaceStep); return ITALIC_STEPS;
    default:          count = sizeof(SANS_STEPS)   / sizeof(FaceStep); return SANS_STEPS;
  }
}

// The widest numeral in whatever font is currently selected. Numerals get
// equal sized boxes so they do not shuffle sideways when a 1 turns into an 8.
static int widestNumeral() {
  int widest = 0;
  for (char c = '0'; c <= '9'; c++) {
    char text[2] = { c, 0 };
    int w = tft.textWidth(text);
    if (w > widest) widest = w;
  }
  return widest;
}

#define MAX_ELEMENTS 6

struct TimeBlock {
  bool      sevenSegment = false;
  const GFXfont *font = nullptr;
  uint8_t   size  = 1;
  int       boxW = 0, boxH = 0, colonW = 0, gap = 0, narrowW = 0;
  int       barThick = 0;
  bool      leadingOne = false;
  int       count = 0;
  char      text[MAX_ELEMENTS];
  int       x[MAX_ELEMENTS];
  int       width[MAX_ELEMENTS];
  int       y = 0;
  TimeBlock() { memset(text, 0, sizeof(text)); memset(x, 0, sizeof(x));
                memset(width, 0, sizeof(width)); }
};

// Works out how big the numerals can be and where each one goes.
// Pass an empty string for hours to lay out just a pair of numerals, which is
// how the seconds in the corner are arranged.
static void layoutTime(TimeBlock &block, const char *hours, const char *minutes,
                       int areaX, int areaY, int areaW, int areaH) {
  int digits = strlen(hours) + strlen(minutes);
  bool withColon = (strlen(hours) > 0);
  block.sevenSegment = (cfg.clockFace == FACE_SEVENSEG);

  // A 1 is only the two bars down the right hand side, and they sit one above
  // the other, so the whole numeral is one bar wide. Giving it a box that size
  // rather than a full width one keeps the time centred on what you can
  // actually see, and leaves the extra room to the other numerals, which come
  // out larger as a result. Only the leading numeral is treated this way.
  // Doing it to the minutes too would shuffle the display about every time a 1
  // came and went.
  bool narrowLead = block.sevenSegment && strlen(hours) > 0 && hours[0] == '1';
  block.leadingOne = narrowLead;

  if (block.sevenSegment) {
    // A numeral is 56 percent as wide as it is tall, a leading 1 just one bar
    // thickness, the colon 24 percent, and the space between them 13 percent.
    // Those are the proportions of a real LED digit. Turning the sum around
    // gives the tallest numeral that still fits the width available.
    float perHeight = (digits - (narrowLead ? 1 : 0)) * 0.56f
                      + (narrowLead ? 0.12f : 0.0f)    // a 1 is one bar wide
                      + (withColon ? 0.24f : 0.0f)
                      + digits * 0.13f;
    int byWidth = (int)(areaW / perHeight);
    block.boxH   = min(byWidth, areaH);
    if (block.boxH < 16) block.boxH = 16;
    // An even height and an even bar thickness keep every corner of every bar
    // on a whole pixel, which is what stops one end coming out a pixel
    // different from the other.
    block.boxH &= ~1;
    block.barThick = (block.boxH * 12 / 100) & ~1;
    if (block.barThick < 4) block.barThick = 4;
    block.boxW   = (int)(block.boxH * 0.56f);
    block.colonW = (int)(block.boxH * 0.24f);
    block.gap    = (int)(block.boxH * 0.13f);
    block.narrowW = block.barThick;   // a 1 is exactly one bar wide
  } else {
    int stepCount = 0;
    const FaceStep *steps = faceSteps(cfg.clockFace, stepCount);
    const FaceStep *chosen = &steps[0];
    for (int i = stepCount - 1; i >= 0; i--) {
      tft.setFreeFont(steps[i].font);
      tft.setTextSize(steps[i].size);
      int boxW   = widestNumeral() + 2 * steps[i].size;
      int colonW = withColon ? tft.textWidth(":") + 2 * steps[i].size : 0;
      int gap    = boxW / 10;
      int tall   = tft.fontHeight();
      int wide   = digits * boxW + colonW + digits * gap;
      if (wide <= areaW && tall <= areaH) { chosen = &steps[i]; break; }
    }
    block.font = chosen->font;
    block.size = chosen->size;
    tft.setFreeFont(block.font);
    tft.setTextSize(block.size);
    block.boxW   = widestNumeral() + 2 * block.size;
    block.colonW = withColon ? tft.textWidth(":") + 2 * block.size : 0;
    block.gap    = block.boxW / 10;
    block.boxH   = tft.fontHeight();
  }

  // Lay the pieces out left to right: hours, colon, minutes.
  block.count = 0;
  for (const char *p = hours; *p; p++) {
    block.text[block.count]  = *p;
    block.width[block.count] = (narrowLead && p == hours) ? block.narrowW : block.boxW;
    block.count++;
  }
  if (withColon) {
    block.text[block.count]  = ':';
    block.width[block.count] = block.colonW;
    block.count++;
  }
  for (const char *p = minutes; *p; p++) {
    block.text[block.count]  = *p;
    block.width[block.count] = block.boxW;
    block.count++;
  }

  int total = 0;
  for (int i = 0; i < block.count; i++) {
    total += block.width[i];
    if (i) total += block.gap;
  }

  // Every box is now exactly as wide as what gets drawn in it, including the
  // box holding a leading 1, so simply centring the boxes centres what you
  // see. No correction needed.
  int left = areaX + (areaW - total) / 2;
  if (left < 1) left = 1;

  for (int i = 0; i < block.count; i++) {
    block.x[i] = left;
    left += block.width[i] + block.gap;
  }
  block.y = areaY + (areaH - block.boxH) / 2;
}

// Paints one numeral, or the colon, inside its box.
static void paintElement(const TimeBlock &block, int index, char ch,
                         uint16_t lit, uint16_t back, uint16_t unlit) {
  int x = block.x[index];
  int w = block.width[index];
  int y = block.y;
  int h = block.boxH;

  if (block.sevenSegment) {
    if (ch == ':')      drawSegmentColon(x, y, w, h, lit, back);
    else if (ch == ' ') drawSegmentColon(x, y, w, h, back, back);
    else if (index == 0 && block.leadingOne)
                        drawNumeralOne(x, y, w, h, block.barThick,
                                       lit, back, cfg.segmentStroke);
    else                drawSevenSegment(x, y, w, h, block.barThick, ch,
                                         lit, unlit, back, cfg.segmentStroke,
                                         cfg.ghostSegments);
    return;
  }

  tft.fillRect(x, y, w, h, back);
  if (ch == ' ') return;
  tft.setFreeFont(block.font);
  tft.setTextSize(block.size);
  tft.setTextColor(lit, back);
  tft.setTextDatum(MC_DATUM);
  char text[2] = { ch, 0 };
  tft.drawString(text, x + w / 2, y + h / 2);
}

// ---------------------------------------------------------------------------
// The clock screen
// ---------------------------------------------------------------------------

static TimeBlock g_block;
static TimeBlock g_seconds;
static char      g_shown[MAX_ELEMENTS]      = { 0 };
static char      g_shownSecs[MAX_ELEMENTS]  = { 0 };
static bool      g_blockValid = false;
static String    g_lastAmPm = "";
static String    g_lastDate = "";

static void forgetClock() {
  g_blockValid = false;
  memset(g_shown, 0, sizeof(g_shown));
  memset(g_shownSecs, 0, sizeof(g_shownSecs));
  g_lastAmPm = "";
  g_lastDate = "";
}

// True when the numerals would land somewhere different or at a different
// size, which means the screen has to be wiped before repainting.
static bool blockMoved(const TimeBlock &a, const TimeBlock &b) {
  if (a.count != b.count || a.boxH != b.boxH || a.boxW != b.boxW ||
      a.narrowW != b.narrowW || a.leadingOne != b.leadingOne ||
      a.y != b.y || a.sevenSegment != b.sevenSegment ||
      a.font != b.font || a.size != b.size) return true;
  for (int i = 0; i < a.count; i++) if (a.x[i] != b.x[i]) return true;
  return false;
}

static void renderClock(struct tm *t) {
  // The loop comes round about a hundred times a second, and almost every one
  // of those passes has nothing new to show. Working the whole layout out
  // again each time, measuring every numeral and formatting the date into
  // fresh strings, was nearly all of core 1's work and a steady churn of small
  // memory blocks. Only go further when something on the screen can change.
  static int  lastMin = -1, lastSec = -1, lastHour = -1;
  static bool lastColon = false;
  bool colonNow = !cfg.blinkColon || ((millis() % 1000UL) < 500UL);
  if (!g_forceFull && g_blockValid &&
      t->tm_min == lastMin && t->tm_hour == lastHour &&
      (!cfg.showSeconds || t->tm_sec == lastSec) && colonNow == lastColon) {
    return;
  }
  lastMin = t->tm_min; lastHour = t->tm_hour; lastSec = t->tm_sec;
  lastColon = colonNow;

  const uint16_t lit   = uiColor(cfg.colorText);
  const uint16_t back  = uiColor(cfg.colorBack);
  const uint16_t unlit = cfg.ghostSegments ? tft.alphaBlend(38, lit, back) : back;
  const int W = tft.width();
  const int H = tft.height();

  int hour = t->tm_hour;
  if (!cfg.use24Hour) { hour %= 12; if (hour == 0) hour = 12; }

  char hours[4], minutes[4], seconds[4];
  snprintf(hours,   sizeof(hours),   cfg.use24Hour ? "%02d" : "%d", hour);
  snprintf(minutes, sizeof(minutes), "%02d", t->tm_min);
  snprintf(seconds, sizeof(seconds), "%02d", t->tm_sec);

  bool showMarker = (!cfg.use24Hour && cfg.showAmPm);
  // The AM or PM marker lives in the bottom right corner. When there is no
  // marker, which is the whole of 24 hour mode, the seconds go down there
  // instead and the numerals get the top of the screen back.
  bool secondsBelow = (cfg.showSeconds && !showMarker);

  // How much room the corners need. The date and the AM or PM marker take one
  // line of the label typeface, and the seconds take a little more than that.
  tft.setFreeFont(fontLabel());
  const int cornerText    = tft.fontHeight() + 6;
  const int cornerSeconds = downY(0.21f);

  int topRoom    = (cfg.showSeconds && !secondsBelow) ? cornerSeconds : 4;
  int bottomRoom = 4;
  if (showMarker || cfg.showDate) bottomRoom = cornerText;
  if (secondsBelow)               bottomRoom = cornerSeconds;

  // A little breathing room down each side. The numerals are still worked out
  // to be as large as will fit, just inside this margin rather than hard
  // against the edge of the glass.
  const int sideMargin = acrossX(0.031f);
  TimeBlock block;
  layoutTime(block, hours, minutes, sideMargin, topRoom,
             W - 2 * sideMargin, H - topRoom - bottomRoom);

  // The seconds get their own small block in a corner, so switching them on
  // does not force the main numerals to shrink. The date always sits in the
  // bottom left.
  TimeBlock secs;
  if (cfg.showSeconds) {
    const int secW = acrossX(0.27f);
    const int secH = downY(0.183f);
    layoutTime(secs, "", seconds, W - secW - 6,
               secondsBelow ? H - secH - 6 : 4, secW, secH);
  }

  bool wipe = g_forceFull || !g_blockValid || blockMoved(block, g_block);
  if (wipe) {
    tft.fillScreen(back);
    forgetClock();
    g_forceFull = false;
  }
  g_block   = block;
  g_seconds = secs;
  g_blockValid = true;

  bool colonLit = colonNow;

  for (int i = 0; i < block.count; i++) {
    char want = block.text[i];
    if (want == ':' && !colonLit) want = ' ';
    if (want != g_shown[i]) {
      paintElement(block, i, want, lit, back, unlit);
      g_shown[i] = want;
    }
  }

  if (cfg.showSeconds) {
    for (int i = 0; i < secs.count; i++) {
      if (secs.text[i] != g_shownSecs[i]) {
        paintElement(secs, i, secs.text[i], lit, back, unlit);
        g_shownSecs[i] = secs.text[i];
      }
    }
  }

  if (showMarker) {
    String marker = (t->tm_hour >= 12) ? "PM" : "AM";
    if (marker != g_lastAmPm) {
      tft.setFreeFont(fontLabel());
      tft.setTextSize(1);
      tft.setTextColor(lit, back);
      tft.setTextDatum(BR_DATUM);
      tft.drawString(marker, W - 6, H - 4);
      g_lastAmPm = marker;
    }
  }

  if (cfg.showDate) {
    char dateBuf[24];
    strftime(dateBuf, sizeof(dateBuf), "%a %b %e", t);
    String dateText = String(dateBuf);
    dateText.replace("  ", " ");
    if (dateText != g_lastDate) {
      tft.setFreeFont(fontLabel());
      tft.fillRect(0, H - tft.fontHeight() - 8, W / 2, tft.fontHeight() + 8, back);
      tft.setTextSize(1);
      tft.setTextColor(lit, back);
      tft.setTextDatum(BL_DATUM);
      tft.drawString(dateText, 6, H - 6);
      g_lastDate = dateText;
    }
  }
}

// ---------------------------------------------------------------------------
// Weather pictures
// ---------------------------------------------------------------------------

static uint16_t COL_SUN, COL_MOON, COL_CLOUD, COL_CLOUD_EDGE, COL_CLOUD_DARK,
                COL_RAIN, COL_SNOW, COL_BOLT, COL_MIST;

static void setupIconColors() {
  COL_SUN        = tft.color565(255, 190,  60);
  COL_MOON       = tft.color565(228, 234, 248);
  COL_CLOUD      = tft.color565(208, 215, 226);
  COL_CLOUD_EDGE = tft.color565(120, 128, 144);
  COL_CLOUD_DARK = tft.color565(148, 156, 170);
  COL_RAIN       = tft.color565( 86, 166, 255);
  COL_SNOW       = tft.color565(240, 248, 255);
  COL_BOLT       = tft.color565(255, 200,  60);
  COL_MIST       = tft.color565(176, 184, 198);
}

// The cloud is built from plain circles and a bar rather than the smoothed
// versions. A smoothed shape softens its own edge against the background, so
// where two of them overlap you get a faint seam running through the middle.
// Drawing the whole outline slightly oversized in a darker colour first, then
// the fill on top, avoids that and hides the stair stepping at the same time.
static void cloudSilhouette(float cx, float cy, float s, float grow, uint16_t color) {
  float r = s * 0.170f;
  tft.fillCircle(cx - s * 0.200f, cy,              r * 1.00f + grow, color);
  tft.fillCircle(cx + s * 0.185f, cy,              r * 0.88f + grow, color);
  tft.fillCircle(cx - s * 0.015f, cy - s * 0.150f, r * 1.30f + grow, color);
  tft.fillRoundRect(cx - s * 0.34f - grow, cy - r * 0.55f - grow,
                    s * 0.68f + 2 * grow, r * 1.55f + 2 * grow,
                    r * 0.70f, color);
}

static void iconCloud(float cx, float cy, float s, uint16_t fill) {
  cloudSilhouette(cx, cy, s, 2.0f, COL_CLOUD_EDGE);
  cloudSilhouette(cx, cy, s, 0.0f, fill);
}

static void iconSun(float cx, float cy, float s, uint16_t back) {
  float inner = s * 0.30f, outer = s * 0.46f, thick = s * 0.075f;
  for (int i = 0; i < 8; i++) {
    float a = i * PI / 4.0f;
    tft.drawWideLine(cx + cosf(a) * inner, cy + sinf(a) * inner,
                     cx + cosf(a) * outer, cy + sinf(a) * outer,
                     thick, COL_SUN, back);
  }
  tft.fillSmoothCircle(cx, cy, s * 0.23f, COL_SUN, back);
}

static void iconMoon(float cx, float cy, float s, uint16_t back) {
  tft.fillSmoothCircle(cx, cy, s * 0.28f, COL_MOON, back);
  // Cutting the crescent. The second circle is painted in the background
  // colour and told to soften towards the moon colour, because the moon is
  // what lies underneath it.
  tft.fillSmoothCircle(cx + s * 0.20f, cy - s * 0.13f, s * 0.26f, back, COL_MOON);
}

static void iconDrops(float cx, float cy, float s, uint16_t color, uint16_t back) {
  for (int i = -1; i <= 1; i++) {
    float x = cx + i * s * 0.19f;
    tft.drawWideLine(x + s * 0.04f, cy, x - s * 0.03f, cy + s * 0.19f,
                     s * 0.055f, color, back);
  }
}

static void iconFlakes(float cx, float cy, float s, uint16_t back) {
  for (int i = -1; i <= 1; i++) {
    float x = cx + i * s * 0.19f;
    float y = cy + s * 0.09f;
    float a = s * 0.075f;
    tft.drawWideLine(x - a, y, x + a, y, s * 0.035f, COL_SNOW, back);
    tft.drawWideLine(x - a * 0.5f, y - a * 0.87f, x + a * 0.5f, y + a * 0.87f,
                     s * 0.035f, COL_SNOW, back);
    tft.drawWideLine(x - a * 0.5f, y + a * 0.87f, x + a * 0.5f, y - a * 0.87f,
                     s * 0.035f, COL_SNOW, back);
    tft.fillSmoothCircle(x, y, s * 0.028f, COL_SNOW, COL_SNOW);
  }
}

static void iconBolt(float cx, float cy, float s) {
  const int16_t px[7] = { (int16_t)(cx + s * 0.07f), (int16_t)(cx - s * 0.12f),
                          (int16_t)(cx - s * 0.01f), (int16_t)(cx - s * 0.09f),
                          (int16_t)(cx + s * 0.13f), (int16_t)(cx + s * 0.02f),
                          (int16_t)(cx + s * 0.11f) };
  const int16_t py[7] = { (int16_t)(cy - s * 0.02f), (int16_t)(cy + s * 0.14f),
                          (int16_t)(cy + s * 0.14f), (int16_t)(cy + s * 0.30f),
                          (int16_t)(cy + s * 0.06f), (int16_t)(cy + s * 0.06f),
                          (int16_t)(cy - s * 0.02f) };
  fillConvex(px, py, 7, COL_BOLT);
}

static void iconMist(float cx, float cy, float s, uint16_t back) {
  for (int i = 0; i < 4; i++) {
    float y = cy - s * 0.15f + i * s * 0.14f;
    float half = (i % 2 == 0) ? s * 0.34f : s * 0.26f;
    tft.drawWideLine(cx - half, y, cx + half, y, s * 0.055f, COL_MIST, back);
  }
}

// Draws the picture for one weather code inside a box of the given size.
static void drawWeatherIcon(int cx, int cy, int size, int code, bool daytime,
                            uint16_t back) {
  float s = (float)size;

  switch (code) {
    case 0:
    case 1:
      if (daytime) iconSun(cx, cy, s, back);
      else         iconMoon(cx, cy, s, back);
      break;

    case 2:
      if (daytime) iconSun(cx - s * 0.17f, cy - s * 0.17f, s * 0.72f, back);
      else         iconMoon(cx - s * 0.17f, cy - s * 0.17f, s * 0.72f, back);
      iconCloud(cx + s * 0.10f, cy + s * 0.14f, s * 0.86f, COL_CLOUD);
      break;

    case 3:
      iconCloud(cx, cy, s, COL_CLOUD);
      break;

    case 45: case 48:
      iconCloud(cx, cy - s * 0.14f, s * 0.90f, COL_CLOUD);
      iconMist(cx, cy + s * 0.24f, s * 0.80f, back);
      break;

    case 51: case 53: case 55: case 56: case 57:
    case 61: case 63: case 65: case 66: case 67:
    case 80: case 81: case 82:
      iconCloud(cx, cy - s * 0.12f, s * 0.92f, COL_CLOUD);
      iconDrops(cx, cy + s * 0.20f, s, COL_RAIN, back);
      break;

    case 71: case 73: case 75: case 77: case 85: case 86:
      iconCloud(cx, cy - s * 0.12f, s * 0.92f, COL_CLOUD);
      iconFlakes(cx, cy + s * 0.20f, s, back);
      break;

    case 95: case 96: case 99:
      iconCloud(cx, cy - s * 0.14f, s * 0.92f, COL_CLOUD_DARK);
      iconBolt(cx, cy + s * 0.16f, s);
      break;

    default:
      iconCloud(cx, cy, s, COL_CLOUD);
      break;
  }
}

// ---------------------------------------------------------------------------
// Small text helpers
// ---------------------------------------------------------------------------

static String tempString(float value) { return String((int)lroundf(value)); }

static const char *dayNameShort(int weekday) {
  static const char *names[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
  if (weekday < 0 || weekday > 6) return "---";
  return names[weekday];
}

static String hourLabel(int hour24) {
  if (hour24 < 0) return "--";
  if (cfg.use24Hour) {
    char buf[6];
    snprintf(buf, sizeof(buf), "%02d", hour24);
    return String(buf);
  }
  int h = hour24 % 12;
  if (h == 0) h = 12;
  return String(h) + (hour24 < 12 ? "a" : "p");
}

static String ageText(uint32_t lastMs) {
  if (lastMs == 0) return "no data";
  uint32_t mins = (millis() - lastMs) / 60000UL;
  if (mins == 0) return "just now";
  if (mins < 60) return String(mins) + "m ago";
  return String(mins / 60) + "h ago";
}

// The degree circle that goes next to a temperature.
static void drawDegree(int x, int y, int radius, uint16_t color, uint16_t back) {
  tft.drawSmoothCircle(x, y, radius, color, back);
  tft.drawSmoothCircle(x, y, radius - 1, color, back);
}

// ---------------------------------------------------------------------------
// Weather screens
// ---------------------------------------------------------------------------

// Draws the strip along the top of a weather screen.
// The right hand side is different on each screen, so it is passed in. The
// note that says a fetch is running always goes on the left, next to the
// title, where there is nothing behind it on any of the three screens.
// Returns the first line below itself that is free to draw on.
static int weatherHeader(const String &leftText, const String &rightText,
                         uint16_t back) {
  const int edge = acrossX(0.02f);
  int lineHeight = useBodyFont();

  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_LIGHTGREY, back);
  tft.drawString(leftText, edge, 2);

  if (weatherIsBusy()) {
    tft.setTextColor(TFT_SKYBLUE, back);
    tft.drawString("updating...", edge + tft.textWidth(leftText) + edge * 2, 2);
  }

  if (rightText.length()) {
    tft.setTextColor(TFT_LIGHTGREY, back);
    tft.setTextDatum(TR_DATUM);
    tft.drawString(rightText, tft.width() - edge, 2);
  }
  return lineHeight + 4;
}

static void renderWeatherNow() {
  const uint16_t lit  = uiColor(cfg.colorText);
  const uint16_t back = uiColor(cfg.colorBack);
  const int W = tft.width();
  const int H = tft.height();

  WeatherData wx;
  weatherGet(wx);

  tft.fillScreen(back);
  // Nothing in the top right when the weather is good. How old the reading is
  // only matters when something has gone wrong, and the status screen shows it
  // anyway. When there is no reading at all, the reason goes there instead.
  weatherHeader("Current Weather", wx.valid ? String("") : wx.status, back);

  if (!wx.valid) {
    tft.setFreeFont(fontLabel());
    tft.setTextSize(1);
    tft.setTextColor(TFT_ORANGE, back);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(weatherIsBusy() ? "Fetching the weather" : "No weather yet",
                   W / 2, downY(0.44f));
    useBodyFont();
    tft.setTextColor(TFT_LIGHTGREY, back);
    tft.drawString(wx.status, W / 2, downY(0.60f));
    return;
  }

  // The big temperature down the left, the picture of the sky on the right.
  const int tempY = downY(0.37f);
  String temp = tempString(wx.temp);
  tft.setFreeFont(&FreeSansBold24pt7b);
  tft.setTextSize(2);
  if (tft.textWidth(temp) > acrossX(0.49f)) tft.setTextSize(1);
  tft.setTextColor(lit, back);
  tft.setTextDatum(ML_DATUM);
  tft.drawString(temp, acrossX(0.05f), tempY);
  drawDegree(acrossX(0.05f) + tft.textWidth(temp) + acrossX(0.044f),
             tempY - tft.fontHeight() / 4, downY(0.034f), lit, back);

  drawWeatherIcon(acrossX(0.79f), downY(0.35f), downY(0.38f),
                  wx.code, wx.isDaytime, back);

  tft.setFreeFont(fontLabel());
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, back);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(weatherText(wx.code), W / 2, downY(0.665f));

  useBodyFont();
  tft.setTextColor(TFT_LIGHTGREY, back);
  tft.drawString(wx.place.length() ? wx.place : cfg.zip, W / 2, downY(0.79f));

  const char *unit = cfg.metric ? "C" : "F";
  String line1 = "Feels " + tempString(wx.feelsLike) + unit +
                 "    High " + tempString(wx.high) + unit +
                 "    Low " + tempString(wx.low) + unit;
  String line2 = "Humidity " + String(wx.humidity) + "%    Wind " +
                 tempString(wx.wind) + (cfg.metric ? " km/h" : " mph");

  tft.drawString(line1, W / 2, downY(0.875f));
  tft.drawString(line2, W / 2, downY(0.95f));
}

static void renderWeatherHourly() {
  const uint16_t back = uiColor(cfg.colorBack);
  const uint16_t lit  = uiColor(cfg.colorText);
  const int W = tft.width();

  WeatherData wx;
  weatherGet(wx);

  tft.fillScreen(back);
  weatherHeader(String(FORECAST_HOUR_SLOTS) + " Hour Forecast",
                wx.place.length() ? wx.place : cfg.zip, back);

  if (!wx.valid || wx.hourCount == 0) {
    tft.setFreeFont(fontLabel());
    tft.setTextSize(1);
    tft.setTextColor(TFT_ORANGE, back);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(weatherIsBusy() ? "Fetching..." : "No hourly data",
                   W / 2, tft.height() / 2);
    return;
  }

  // Four hours across the screen in a single row, starting with the next one.
  const int cellWidth = W / 4;

  for (int i = 0; i < wx.hourCount && i < FORECAST_HOUR_SLOTS; i++) {
    int cx = i * cellWidth + cellWidth / 2;

    tft.setFreeFont(fontLabel());
    tft.setTextSize(1);
    tft.setTextColor(TFT_LIGHTGREY, back);
    tft.setTextDatum(TC_DATUM);
    tft.drawString(hourLabel(wx.hours[i].hour24), cx, downY(0.135f));

    bool daytime = (wx.hours[i].hour24 >= 6 && wx.hours[i].hour24 < 20);
    drawWeatherIcon(cx, downY(0.50f), downY(0.32f), wx.hours[i].code, daytime, back);

    tft.setFreeFont(bigScreen() ? &FreeSansBold24pt7b : &FreeSansBold18pt7b);
    tft.setTextSize(1);
    tft.setTextColor(lit, back);
    tft.setTextDatum(TC_DATUM);
    tft.drawString(tempString(wx.hours[i].temp), cx, downY(0.715f));
  }
}

static void renderWeatherDaily() {
  const uint16_t back = uiColor(cfg.colorBack);
  const uint16_t lit  = uiColor(cfg.colorText);
  const int W = tft.width();
  const int H = tft.height();

  WeatherData wx;
  weatherGet(wx);

  // Where each column of the rows below sits. The headings use the same
  // numbers, so they cannot drift out of line with what is underneath them.
  const int dayX   = acrossX(0.025f);
  const int iconX  = acrossX(0.35f);
  const int condX  = acrossX(0.46f);
  const int highX  = acrossX(0.88f);
  const int lowX   = acrossX(0.99f);

  tft.fillScreen(back);
  int topArea = weatherHeader(String(cfg.forecastDays) + " Day Forecast", "", back);

  // Column headings that line up with the numbers in each row below.
  tft.setTextColor(TFT_DARKGREY, back);
  tft.setTextDatum(TR_DATUM);
  tft.drawString("high", highX, 2);
  tft.drawString("low", lowX, 2);

  if (!wx.valid || wx.dayCount == 0) {
    tft.setFreeFont(fontLabel());
    tft.setTextSize(1);
    tft.setTextColor(TFT_ORANGE, back);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(weatherIsBusy() ? "Fetching..." : "No forecast yet", W / 2, H / 2);
    return;
  }

  int count     = wx.dayCount;
  int rowHeight = (H - topArea) / count;

  // Ten days on a small screen leaves very little room per row, so the text
  // drops to the smaller face when the rows get tight rather than overlapping.
  bool roomy = (rowHeight >= bodyLineHeight() + 6);
  uint8_t rowFont = roomy ? fontBody() : 2;

  for (int i = 0; i < count; i++) {
    int cy = topArea + i * rowHeight + rowHeight / 2;

    tft.setTextFont(rowFont);
    tft.setTextSize(1);
    tft.setTextColor(i == 0 ? lit : TFT_WHITE, back);
    tft.setTextDatum(ML_DATUM);
    tft.drawString(i == 0 ? "Today" : dayNameShort(wx.days[i].weekday), dayX, cy);

    int iconSize = rowHeight;
    if (iconSize > downY(0.14f)) iconSize = downY(0.14f);
    drawWeatherIcon(iconX, cy, iconSize, wx.days[i].code, true, back);

    tft.setTextColor(TFT_LIGHTGREY, back);
    tft.setTextDatum(ML_DATUM);
    tft.drawString(weatherTextShort(wx.days[i].code), condX, cy);

    tft.setTextColor(TFT_WHITE, back);
    tft.setTextDatum(MR_DATUM);
    tft.drawString(tempString(wx.days[i].high), highX, cy);
    tft.setTextColor(TFT_SILVER, back);
    tft.drawString(tempString(wx.days[i].low), lowX, cy);
  }
}

// ---------------------------------------------------------------------------
// Status screen
// ---------------------------------------------------------------------------

// Where the buttons sit.
//
// These are worked out from the screen size each time they are needed, rather
// than typed in, because the drawing and the touch test both have to agree on
// exactly the same rectangle. One function, used by both, cannot disagree
// with itself.
struct Rect { int x, y, w, h; };

static bool within(int x, int y, const Rect &r) {
  return (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h);
}

static Rect resetButtonRect() {
  int w = acrossX(0.44f);
  int h = downY(0.14f);
  return Rect{ (tft.width() - w) / 2, downY(0.83f), w, h };
}

static Rect confirmEraseRect() {
  return Rect{ acrossX(0.075f), downY(0.62f), acrossX(0.39f), downY(0.23f) };
}

static Rect confirmKeepRect() {
  int w = acrossX(0.39f);
  return Rect{ tft.width() - acrossX(0.075f) - w, downY(0.62f), w, downY(0.23f) };
}

static void renderStatus(struct tm *t, bool timeValid) {
  const uint16_t back = uiColor(cfg.colorBack);
  const uint16_t lit  = uiColor(cfg.colorText);
  const int W = tft.width();

  WeatherData wx;
  weatherGet(wx);

  tft.fillScreen(back);
  tft.setFreeFont(fontLabel());
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, back);
  tft.setTextDatum(TC_DATUM);
  tft.drawString(FW_NAME " " FW_VERSION, W / 2, 2);
  int listTop = tft.fontHeight() + 6;

  bool online = (WiFi.status() == WL_CONNECTED);
  String ip = online ? WiFi.localIP().toString() : String("none");

  int enabledAlarms = 0;
  for (int i = 0; i < ALARM_COUNT; i++) if (cfg.alarms[i].enabled) enabledAlarms++;

  unsigned long upSeconds = millis() / 1000UL;
  String uptime = String(upSeconds / 3600UL) + "h " +
                  String((upSeconds / 60UL) % 60UL) + "m";

  struct { const char *label; String value; uint16_t color; } lines[] = {
    { "WiFi",    online ? (WiFi.SSID() + "  " + String(WiFi.RSSI()) + " dBm")
                        : String("not connected"),
                 (uint16_t)(online ? TFT_GREEN : TFT_RED) },
    { "Name",    deviceHostname() + ".local", TFT_WHITE },
    { "NTP",     cfg.ntpServer + (timeValid ? "  synced" : "  waiting"),
                 (uint16_t)(timeValid ? TFT_GREEN : TFT_ORANGE) },
    { "Zone",    cfg.timeZoneName, TFT_WHITE },
    { "Weather", (wx.place.length() ? wx.place : cfg.zip) + "  " +
                 (wx.valid ? ageText(wx.lastUpdateMs) : wx.status),
                 (uint16_t)(wx.valid ? TFT_GREEN : TFT_ORANGE) },
    { "Alarms",  String(enabledAlarms) + " of " + String(ALARM_COUNT) + " on   up " +
                 uptime, TFT_WHITE },
  };

  // The address to browse to is the one thing on this screen anybody needs to
  // act on, so it is measured and given its room first, in the largest
  // typeface that fits. The list of details then takes whatever is left.
  const Rect button  = resetButtonRect();
  const int  edge    = acrossX(0.025f);
  const int  widest  = W - 2 * edge;

  String browseTop = online ? ("Browse to http://" + ip)
                            : String("Connect to the " SETUP_AP_NAME " network");
  String browseBot = online ? String("to configure your clock") : String("");

  int browseH1 = useFontThatFits(browseTop, widest, 1);
  int browseH2 = browseBot.length() ? useFontThatFits(browseBot, widest, 1) : 0;
  int browseBlock = browseH1 + browseH2 + 6;
  int browseY  = button.y - browseBlock - 6;

  int rows  = (int)(sizeof(lines) / sizeof(lines[0]));
  int space = browseY - listTop - 6;
  int step  = space / rows;
  tft.setTextFont(step >= 26 ? 4 : 2);
  tft.setTextSize(1);

  // Put the values in a column just clear of the longest label, so nothing
  // collides whichever typeface ended up being used.
  int labelWidth = 0;
  for (auto &line : lines) {
    int w = tft.textWidth(String(line.label) + ":  ");
    if (w > labelWidth) labelWidth = w;
  }
  const int valueX = edge + labelWidth;

  int y = listTop;
  tft.setTextDatum(TL_DATUM);
  for (auto &line : lines) {
    tft.setTextColor(TFT_SILVER, back);
    tft.drawString(String(line.label) + ":", edge, y);
    tft.setTextColor(line.color, back);
    tft.drawString(line.value, valueX, y);
    y += step;
  }

  // The address, in the room set aside for it above the button.
  tft.setTextColor(TFT_WHITE, back);
  tft.setTextDatum(TC_DATUM);
  useFontThatFits(browseTop, widest, 1);
  tft.drawString(browseTop, W / 2, browseY);
  if (browseBot.length()) {
    useFontThatFits(browseBot, widest, 1);
    tft.drawString(browseBot, W / 2, browseY + browseH1 + 2);
  }

  // The factory reset button. It only asks the question here. Nothing is
  // erased until the confirmation screen.
  tft.drawRoundRect(button.x, button.y, button.w, button.h, 6, TFT_RED);
  tft.drawRoundRect(button.x + 1, button.y + 1, button.w - 2, button.h - 2, 6, TFT_RED);
  useBodyFont();
  tft.setTextColor(TFT_RED, back);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Factory Reset", button.x + button.w / 2, button.y + button.h / 2);
  (void)t;
}

// ---------------------------------------------------------------------------
// Are you sure screen
// ---------------------------------------------------------------------------

static void renderResetConfirm() {
  const uint16_t back = uiColor(cfg.colorBack);
  const int W = tft.width();

  tft.fillScreen(back);
  tft.setFreeFont(fontLabel());
  tft.setTextSize(1);
  tft.setTextColor(TFT_RED, back);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("Erase everything?", W / 2, downY(0.05f));

  // The three body lines all share one typeface, so pick it from the longest
  // of them and use that for all three.
  const char *body[3] = { "This clears every setting and forgets your",
                          "WiFi network. The clock ends up exactly",
                          "as it was when you first flashed it." };
  const int widest = W - acrossX(0.05f);
  int longest = 0;
  useRung(2);
  for (int i = 1; i < 3; i++) {
    if (tft.textWidth(body[i]) > tft.textWidth(body[longest])) longest = i;
  }
  int lineHeight = useFontThatFits(body[longest], widest, 2);

  tft.setTextColor(TFT_WHITE, back);
  int y = downY(0.225f);
  for (int i = 0; i < 3; i++) tft.drawString(body[i], W / 2, y + lineHeight * i);

  const Rect erase = confirmEraseRect();
  const Rect keep  = confirmKeepRect();

  tft.fillRoundRect(erase.x, erase.y, erase.w, erase.h, 8, TFT_RED);
  tft.setTextColor(TFT_WHITE, TFT_RED);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Erase", erase.x + erase.w / 2, erase.y + erase.h / 2);

  tft.fillRoundRect(keep.x, keep.y, keep.w, keep.h, 8, TFT_DARKGREY);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft.drawString("Keep it", keep.x + keep.w / 2, keep.y + keep.h / 2);

  tft.setTextColor(TFT_LIGHTGREY, back);
  tft.setTextDatum(BC_DATUM);
  const char *hint = "Touch elsewhere to keep your settings";
  useFontThatFits(hint, widest, 2);
  tft.drawString(hint, W / 2, tft.height() - 6);
}

TouchTarget uiHitTest(int x, int y) {
  if (g_screen == SCREEN_STATUS) {
    if (within(x, y, resetButtonRect())) return TOUCH_RESET_BUTTON;
  } else if (g_screen == SCREEN_RESET_CONFIRM) {
    if (within(x, y, confirmEraseRect())) return TOUCH_RESET_ERASE;
    if (within(x, y, confirmKeepRect()))  return TOUCH_RESET_CANCEL;
  }
  return TOUCH_NOTHING;
}

// ---------------------------------------------------------------------------
// Alarm screen
// ---------------------------------------------------------------------------

static void renderAlarm(struct tm *t, bool inverted) {
  const int W = tft.width();
  uint16_t back = inverted ? uiColor(cfg.colorText) : uiColor(cfg.colorBack);
  uint16_t text = inverted ? uiColor(cfg.colorBack) : uiColor(cfg.colorText);

  tft.fillScreen(back);
  tft.setTextColor(text, back);
  tft.setTextDatum(MC_DATUM);
  tft.setFreeFont(&FreeSansBold24pt7b);
  tft.setTextSize(1);
  tft.drawString("ALARM", W / 2, downY(0.225f));

  int hour = t->tm_hour;
  if (!cfg.use24Hour) { hour %= 12; if (hour == 0) hour = 12; }
  char buf[8];
  snprintf(buf, sizeof(buf), cfg.use24Hour ? "%02d:%02d" : "%d:%02d", hour, t->tm_min);
  tft.setTextSize(2);
  tft.drawString(buf, W / 2, downY(0.575f));

  useBodyFont();
  tft.drawString("Touch the screen to stop", W / 2, downY(0.89f));
}

// ---------------------------------------------------------------------------
// Message screen
// ---------------------------------------------------------------------------

void uiMessage(const String &line1, const String &line2, const String &line3,
               const String &line4, uint16_t color) {
  const int W = tft.width();
  const int H = tft.height();
  const uint16_t back = uiColor(cfg.colorBack);
  g_screen = SCREEN_MESSAGE;

  tft.fillScreen(back);
  tft.setTextDatum(TC_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(color, back);

  // The first two lines are drawn large and the last two small. How tall each
  // one is comes from the typeface actually in use, so the block stays centred
  // on either screen.
  tft.setFreeFont(fontLabel());
  const int tallLine  = tft.fontHeight() + 4;
  const int smallLine = bodyLineHeight();

  const String *lines[4] = { &line1, &line2, &line3, &line4 };
  const bool    large[4] = { true, true, false, false };

  int total = 0;
  for (int i = 0; i < 4; i++) {
    if (lines[i]->length()) total += large[i] ? tallLine : smallLine;
  }

  int y = (H - total) / 2;
  for (int i = 0; i < 4; i++) {
    if (!lines[i]->length()) continue;
    if (large[i]) { tft.setFreeFont(fontLabel()); tft.setTextSize(1); }
    else          { useBodyFont(); }
    tft.setTextColor(color, back);
    tft.drawString(*lines[i], W / 2, y);
    y += large[i] ? tallLine : smallLine;
  }
}

// ---------------------------------------------------------------------------
// The setup network screen
// ---------------------------------------------------------------------------

// Draws a line of text across the middle of the screen. If it would not fit,
// it drops to the smaller face rather than running off both edges.
static void drawCentredFit(const String &text, int y, uint16_t color, uint16_t back) {
  tft.setTextColor(color, back);
  tft.setTextDatum(TC_DATUM);
  tft.setTextSize(1);
  // Step down through the sizes until it fits, rather than letting a long
  // line run off both edges of the screen.
  tft.setTextFont(fontBody());
  if (tft.textWidth(text) > tft.width() - 8) tft.setTextFont(2);
  if (tft.textWidth(text) > tft.width() - 8) tft.setTextFont(1);
  tft.drawString(text, tft.width() / 2, y);
}

// Shown once the clock is on your network. Both addresses are here because
// either will reach the settings page, and on some networks the name does not
// resolve, so the numeric one is the fallback that always works.
//
// Every line is sized to fit rather than being given a fixed size, and the
// whole block is centred by its measured height, so the layout holds whatever
// length of address it ends up having to show.
void uiConfigureScreen(const String &ipUrl, const String &nameUrl) {
  const uint16_t back = uiColor(cfg.colorBack);
  const int W = tft.width();
  const int H = tft.height();
  const int widest = W - acrossX(0.05f);
  g_screen = SCREEN_MESSAGE;

  struct Line { String text; uint16_t colour; int from; };
  Line lines[] = {
    { "Configure your clock at:", TFT_WHITE,     1 },
    { ipUrl,                      TFT_GREEN,     0 },
    { "or",                       TFT_LIGHTGREY, 2 },
    { nameUrl,                    TFT_GREEN,     0 },
    { "Long press the screen",    TFT_WHITE,     2 },
    { "to see this info again.",  TFT_WHITE,     2 },
  };
  const int count = (int)(sizeof(lines) / sizeof(lines[0]));

  // Measure first, so the block can be centred as a whole rather than each
  // line being placed at a guessed position.
  int heights[6];
  int total = 0;
  for (int i = 0; i < count; i++) {
    heights[i] = useFontThatFits(lines[i].text, widest, lines[i].from);
    total += heights[i] + downY(0.02f);
  }

  tft.fillScreen(back);
  tft.setTextDatum(TC_DATUM);

  int y = (H - total) / 2;
  if (y < 2) y = 2;
  for (int i = 0; i < count; i++) {
    useFontThatFits(lines[i].text, widest, lines[i].from);
    tft.setTextColor(lines[i].colour, back);
    tft.drawString(lines[i].text, W / 2, y);
    y += heights[i] + downY(0.02f);
  }
}

void uiPortalScreen(const String &ssid, const String &address, int clients) {
  const uint16_t back = uiColor(cfg.colorBack);
  const int W = tft.width();
  const int H = tft.height();
  g_screen = SCREEN_MESSAGE;

  tft.fillScreen(back);

  drawCentredFit("On your phone, join this WiFi network", downY(0.033f),
                 TFT_LIGHTGREY, back);

  tft.setFreeFont(fontLabel());
  tft.setTextSize(1);
  tft.setTextColor(TFT_GREEN, back);
  tft.setTextDatum(TC_DATUM);
  tft.drawString(ssid, W / 2, downY(0.125f));

  drawCentredFit("It is open. There is no password.", downY(0.26f),
                 TFT_LIGHTGREY, back);

  tft.drawFastHLine(acrossX(0.106f), downY(0.383f), W - acrossX(0.212f), TFT_DARKGREY);

  drawCentredFit(clients > 0 ? "If no setup page opened by itself,"
                             : "If no setup page opens by itself,",
                 downY(0.425f), TFT_LIGHTGREY, back);
  drawCentredFit("open a browser and go to", downY(0.50f), TFT_LIGHTGREY, back);

  tft.setFreeFont(fontLabel());
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, back);
  tft.setTextDatum(TC_DATUM);
  tft.drawString(address, W / 2, downY(0.60f));

  if (clients > 0) {
    drawCentredFit("Phone connected", downY(0.79f), TFT_GREEN, back);
    drawCentredFit("Now pick your network and save", downY(0.875f), TFT_LIGHTGREY, back);
  } else {
    drawCentredFit("Waiting for a phone to join...", downY(0.79f), TFT_YELLOW, back);
    drawCentredFit("Turning mobile data off can help", downY(0.875f), TFT_LIGHTGREY, back);
  }
  (void)H;
}

// ---------------------------------------------------------------------------
// Screen switching and the repaint loop
// ---------------------------------------------------------------------------

// Some panels treat a colour value as its opposite, so a red clock on a black
// background comes out cyan on white. This turns that round. It takes effect
// straight away, with no restart.
void uiApplyInvert() {
  tft.invertDisplay(cfg.invertColors);
}

void uiBegin() {
  tft.init();
  tft.setRotation(1);              // landscape, 320 across by 240 down
  uiApplyInvert();

  ledcSetup(PWM_CHANNEL_BACKLIGHT, 5000, 8);
  ledcAttachPin(PIN_TFT_BACKLIGHT, PWM_CHANNEL_BACKLIGHT);
  uiSetBrightness(cfg.brightness);

  setupIconColors();
  tft.fillScreen(uiColor(cfg.colorBack));
  g_forceFull = true;
}

void uiRedraw() {
  g_forceFull = true;
  forgetClock();
}

ScreenId uiCurrentScreen() { return g_screen; }

static bool isWeatherScreen(ScreenId s) {
  return s == SCREEN_WEATHER_NOW || s == SCREEN_WEATHER_HOURLY ||
         s == SCREEN_WEATHER_DAILY;
}

void uiSetScreen(ScreenId screen) {
  if (screen != g_screen) {
    g_screen = screen;
    uiRedraw();
  }
  g_screenEnteredMs = millis();

  // Landing on a weather screen is what asks for a fresh reading.
  if (isWeatherScreen(screen)) weatherRequestOnDemand();
}

void uiNextScreen() {
  ScreenId order[4];
  int count = 0;
  order[count++] = SCREEN_CLOCK;
  if (cfg.showCurrentScreen) order[count++] = SCREEN_WEATHER_NOW;
  if (cfg.showHourlyScreen)  order[count++] = SCREEN_WEATHER_HOURLY;
  if (cfg.showDailyScreen)   order[count++] = SCREEN_WEATHER_DAILY;

  if (g_screen == SCREEN_STATUS || g_screen == SCREEN_MESSAGE) {
    uiSetScreen(SCREEN_CLOCK);
    return;
  }

  int position = 0;
  for (int i = 0; i < count; i++) if (order[i] == g_screen) position = i;
  uiSetScreen(order[(position + 1) % count]);
}

void uiTick(struct tm *timeNow, bool timeValid) {
  static uint32_t lastAlarmFrame = 0;
  static bool     alarmInverted  = false;
  static uint32_t seenWeather    = 0;
  static bool     seenBusy       = false;
  static bool     wasTimeValid   = false;

  if (alarmIsActive()) {
    // The screen is painted once and then flashed by flipping the panel's own
    // invert bit, which is a single command. It used to be repainted from
    // scratch two and a half times a second: a hundred and fifty kilobytes
    // down the wire each time, and a lump of current drawn with it. That was
    // enough to disturb the little amplifier sitting next to it, and the sound
    // would drop out in step with the flashing.
    if (g_screen != SCREEN_ALARM) {
      uiSetScreen(SCREEN_ALARM);
      alarmInverted = false;
      if (timeValid) renderAlarm(timeNow, false);
      lastAlarmFrame = millis();
    }
    if (alarmActiveUsesScreen() && millis() - lastAlarmFrame > 400) {
      lastAlarmFrame = millis();
      alarmInverted = !alarmInverted;
      tft.invertDisplay(cfg.invertColors != alarmInverted);
    }
    return;
  }
  if (g_screen == SCREEN_ALARM) {
    alarmInverted = false;
    uiApplyInvert();                   // put the panel back the way it was
    uiSetScreen(SCREEN_CLOCK);
  }

  uint32_t showingFor = millis() - g_screenEnteredMs;
  if (g_screen == SCREEN_STATUS && showingFor > STATUS_RETURN_MS) {
    uiSetScreen(SCREEN_CLOCK);
  } else if (g_screen == SCREEN_RESET_CONFIRM && showingFor > STATUS_RETURN_MS) {
    uiSetScreen(SCREEN_CLOCK);          // asking and walking away changes nothing
  } else if (isWeatherScreen(g_screen) && showingFor > SCREEN_RETURN_MS) {
    uiSetScreen(SCREEN_CLOCK);
  }

  // A weather screen repaints the moment new readings land, and also when a
  // fetch starts or finishes so the updating note appears and clears.
  if (isWeatherScreen(g_screen)) {
    uint32_t version = weatherVersion();
    bool busy = weatherIsBusy();
    if (version != seenWeather || busy != seenBusy) {
      seenWeather = version;
      seenBusy    = busy;
      g_forceFull = true;
    }
  }

  switch (g_screen) {
    case SCREEN_CLOCK:
      if (!timeValid) {
        static uint32_t lastWaitPaint = 0;
        if (g_forceFull || millis() - lastWaitPaint > 3000) {
          lastWaitPaint = millis();
          uiMessage("Waiting for the time", "", "Server: " + cfg.ntpServer, "",
                    TFT_YELLOW);
          g_screen    = SCREEN_CLOCK;
          g_forceFull = false;
        }
        wasTimeValid = false;
        return;
      }
      if (!wasTimeValid) { wasTimeValid = true; uiRedraw(); }
      renderClock(timeNow);
      break;

    case SCREEN_WEATHER_NOW:
      if (g_forceFull) { renderWeatherNow(); g_forceFull = false; }
      break;

    case SCREEN_WEATHER_HOURLY:
      if (g_forceFull) { renderWeatherHourly(); g_forceFull = false; }
      break;

    case SCREEN_WEATHER_DAILY:
      if (g_forceFull) { renderWeatherDaily(); g_forceFull = false; }
      break;

    case SCREEN_RESET_CONFIRM:
      if (g_forceFull) { renderResetConfirm(); g_forceFull = false; }
      break;

    case SCREEN_STATUS: {
      static uint32_t lastStatusPaint = 0;
      if (g_forceFull || millis() - lastStatusPaint > 5000) {
        lastStatusPaint = millis();
        renderStatus(timeNow, timeValid);
        g_forceFull = false;
      }
      break;
    }

    default:
      break;
  }
}
