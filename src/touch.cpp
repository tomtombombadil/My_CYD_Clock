// touch.cpp

#include "touch.h"
#include "config.h"
#include "display.h"
#include "logbuf.h"

#if TOUCH_SHARES_DISPLAY_BUS
  // Nothing extra to include. The display library has the touch panel, and
  // tft is already declared in display.h.
#else
  #include <SPI.h>
  #include <XPT2046_Touchscreen.h>

  // The touch panel gets the second of the chip's two SPI controllers. The
  // display has the first one. That is set in platformio.ini.
  static SPIClass            touchBus(VSPI);
  static XPT2046_Touchscreen panel(PIN_TOUCH_CS, PIN_TOUCH_IRQ);
#endif

// Turns a raw reading from the panel into a position on the screen.
//
// The panel does not report pixels. It reports roughly where between its own
// two edges the finger is, as a number in the low hundreds to the high three
// thousands. Those ends are listed per board in boards.h.
//
// The panel is also built the tall way round while the clock runs sideways,
// so on some boards its two directions have to be swapped to match what you
// are actually looking at.
static void rawToScreen(int rawX, int rawY, int &x, int &y) {
  int acrossRaw = rawX, downRaw = rawY;

#if TOUCH_SWAP_XY
  acrossRaw = rawY;
  downRaw   = rawX;
#endif

  int w = tft.width(), h = tft.height();
  x = map(acrossRaw, TOUCH_RAW_LEFT, TOUCH_RAW_RIGHT, 0, w - 1);
  y = map(downRaw,   TOUCH_RAW_TOP,  TOUCH_RAW_BOTTOM, 0, h - 1);

#if TOUCH_FLIP_X
  x = (w - 1) - x;
#endif
#if TOUCH_FLIP_Y
  y = (h - 1) - y;
#endif

  x = constrain(x, 0, w - 1);
  y = constrain(y, 0, h - 1);
}

void touchBegin() {
#if TOUCH_SHARES_DISPLAY_BUS
  logLine("Touch panel shares the display's wires");
#else
  touchBus.begin(PIN_TOUCH_CLK, PIN_TOUCH_MISO, PIN_TOUCH_MOSI, PIN_TOUCH_CS);
  panel.begin(touchBus);
  panel.setRotation(1);
  logLine("Touch panel has its own wires");
#endif
}

bool touchIsDown() {
#if TOUCH_SHARES_DISPLAY_BUS
  // The display library reports how hard the screen is being pressed. Light
  // readings are noise rather than a finger.
  return tft.getTouchRawZ() > 400;
#else
  return panel.touched();
#endif
}

void touchGetPoint(int &x, int &y) {
  int rawX = 0, rawY = 0;

#if TOUCH_SHARES_DISPLAY_BUS
  uint16_t rx = 0, ry = 0;
  tft.getTouchRaw(&rx, &ry);
  rawX = rx;
  rawY = ry;
#else
  TS_Point p = panel.getPoint();
  rawX = p.x;
  rawY = p.y;
#endif

  rawToScreen(rawX, rawY, x, y);

  // Printed so that the numbers in boards.h can be checked against a real
  // finger on a real screen, which is the only way to get them right.
  logLine("Touch at " + String(x) + "," + String(y) +
          "  (panel reported " + String(rawX) + "," + String(rawY) + ")");
}
