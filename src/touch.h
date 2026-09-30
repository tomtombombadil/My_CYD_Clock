// touch.h
// Reading the touch panel, on either of the two ways these boards wire it.
//
// On the 2.8 inch board the touch panel has four wires of its own, separate
// from the display, and a library called XPT2046_Touchscreen drives it.
//
// On the 3.2 and 3.5 inch boards the touch panel is wired to the same four
// wires as the display. Two things cannot drive the same wires independently,
// so on those boards the display library reads the touch panel as well.
//
// The rest of the program does not need to know which of those is happening.
// It asks the three questions below and gets the same answers either way.

#pragma once
#include <Arduino.h>

void touchBegin();

// True while a finger is on the screen.
bool touchIsDown();

// Where the screen was last touched, in pixels from the top left corner.
// Only meaningful just after touchIsDown() has returned true.
void touchGetPoint(int &x, int &y);
