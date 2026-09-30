// webui.h
// The settings web page the clock hosts on its own address.

#pragma once
#include <Arduino.h>

void webBegin();

// Call from the main loop. Handles the delayed restart after a WiFi change.
void webTick();
