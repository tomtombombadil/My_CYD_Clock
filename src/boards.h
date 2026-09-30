// boards.h
// Everything that is different from one Cheap Yellow Display to another.
//
// Three boards are supported. Which one you are building for is decided in
// platformio.ini, which sets one of BOARD_CYD28, BOARD_CYD32 or BOARD_CYD35.
// Nothing else in the program needs to know which board it is on: it asks the
// questions below instead.
//
//   BOARD_CYD28   2.8 inch, ESP32-2432S028R, 240x320
//   BOARD_CYD32   3.2 inch, ESP32-2432S032R, 240x320
//   BOARD_CYD35   3.5 inch, ESP32-3248S035R, 320x480
//                 Also covers the 4.0 inch board that uses the same panel and
//                 the same controller, just a larger sheet of glass.
//
// The display pins themselves are not here. The display library reads those
// when the program is compiled, so they live in platformio.ini with the rest
// of the build settings.

#pragma once
#include <Arduino.h>

// ---------------------------------------------------------------------------
// Things that are the same on all three boards
// ---------------------------------------------------------------------------

// The small colour LED on the back. These pins are wired so that pulling one
// low turns that colour on, which is backwards from what you might expect.
#define PIN_LED_RED         4
#define PIN_LED_GREEN       16
#define PIN_LED_BLUE        17

// The two pin speaker header, marked SPEAK on the board.
#define PIN_SPEAKER         26

// The touch panel's "something is being touched" line.
#define PIN_TOUCH_IRQ       36

// ---------------------------------------------------------------------------
// 2.8 inch
// ---------------------------------------------------------------------------
#if defined(BOARD_CYD28)

  #define BOARD_NAME            "2.8 inch"

  // The backlight sits on a different pin on this board than on the other two.
  #define PIN_TFT_BACKLIGHT     21

  // The touch panel has its own four wires, separate from the display. That
  // means it needs its own SPI controller, and the XPT2046 library drives it.
  #define TOUCH_SHARES_DISPLAY_BUS  0
  #define PIN_TOUCH_MOSI        32
  #define PIN_TOUCH_MISO        39
  #define PIN_TOUCH_CLK         25
  #define PIN_TOUCH_CS          33

  // Turning what the panel reports into a position on the screen. The panel
  // gives a raw reading rather than a pixel, and the usable range varies a
  // little from board to board. See the note at the bottom of this file if a
  // button feels like it is in the wrong place.
  #define TOUCH_RAW_LEFT        200
  #define TOUCH_RAW_RIGHT       3700
  #define TOUCH_RAW_TOP         240
  #define TOUCH_RAW_BOTTOM      3800
  #define TOUCH_SWAP_XY         0

  // Panels differ in whether they treat a colour value as the colour to show
  // or as its opposite. This is what the Invert colours setting starts out as
  // on this board. It can be changed on the settings page at any time.
  #define BOARD_DEFAULT_INVERT  false

// ---------------------------------------------------------------------------
// 3.2 inch
// ---------------------------------------------------------------------------
#elif defined(BOARD_CYD32)

  #define BOARD_NAME            "3.2 inch"
  #define PIN_TFT_BACKLIGHT     27

  // On this board the touch panel is wired to the same four wires as the
  // display. Two things cannot drive the same wires independently, so the
  // display library handles the touch panel here as well. There are no
  // separate pins to list.
  #define TOUCH_SHARES_DISPLAY_BUS  1

  #define TOUCH_RAW_LEFT        300
  #define TOUCH_RAW_RIGHT       3800
  #define TOUCH_RAW_TOP         300
  #define TOUCH_RAW_BOTTOM      3800
  // The panel is built portrait and the clock runs landscape, so the panel's
  // two directions have to be swapped to match what you see.
  #define TOUCH_SWAP_XY         1
  #define TOUCH_FLIP_X          1
  #define TOUCH_FLIP_Y          0

  // This board's panel comes up showing every colour as its opposite, so it
  // starts out inverted. Measured on a real one rather than guessed.
  #define BOARD_DEFAULT_INVERT  true

  // The speaker amplifier only runs while GPIO 4 is held low, and GPIO 4 is
  // also the red LED. Found on a real board: with the LED option off an alarm
  // made no sound at all, and with it on, sound only came through while the
  // LED was lit. So the pin is held low whenever a sound plays.
  #define AMP_NEEDS_RED_PIN_LOW 1

// ---------------------------------------------------------------------------
// 3.5 inch (and the 4.0 inch board built on the same panel)
// ---------------------------------------------------------------------------
#elif defined(BOARD_CYD35)

  #define BOARD_NAME            "3.5 inch"
  #define PIN_TFT_BACKLIGHT     27

  #define TOUCH_SHARES_DISPLAY_BUS  1

  #define TOUCH_RAW_LEFT        150
  #define TOUCH_RAW_RIGHT       3800
  #define TOUCH_RAW_TOP         200
  #define TOUCH_RAW_BOTTOM      3850
  #define TOUCH_SWAP_XY         1
  #define TOUCH_FLIP_X          1
  #define TOUCH_FLIP_Y          0
  #define BOARD_DEFAULT_INVERT  false

  // Same as the 3.2 inch: the speaker amplifier only runs while GPIO 4, the
  // red LED, is held low. Found on a real 4.0 inch board.
  #define AMP_NEEDS_RED_PIN_LOW 1

#else
  #error "No board chosen. Pick one of the cyd28, cyd32 or cyd35 build setups in platformio.ini."
#endif

// Sensible defaults so the settings below can be left out above.
#ifndef AMP_NEEDS_RED_PIN_LOW
  #define AMP_NEEDS_RED_PIN_LOW 0
#endif
#ifndef TOUCH_FLIP_X
  #define TOUCH_FLIP_X 0
#endif
#ifndef TOUCH_FLIP_Y
  #define TOUCH_FLIP_Y 0
#endif

// ---------------------------------------------------------------------------
// If a button feels like it is in the wrong place
// ---------------------------------------------------------------------------
// Only one thing in this clock cares where you touched rather than just that
// you did: the factory reset button on the touch and hold screen. Everything
// else is a tap anywhere.
//
// If that button will not respond, or responds when you touch somewhere else,
// the four TOUCH_RAW numbers for your board are what to adjust. Raising
// TOUCH_RAW_LEFT moves touches to the left, raising TOUCH_RAW_TOP moves them
// up, and so on. If touches seem to run the wrong way along an edge entirely,
// change TOUCH_FLIP_X or TOUCH_FLIP_Y from 0 to 1.
//
// The clock prints the position of every touch to the serial monitor, so you
// can touch each corner of the screen in turn and see what numbers come back.
