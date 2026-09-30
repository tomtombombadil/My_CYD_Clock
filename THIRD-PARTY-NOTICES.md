# Third party notices

The firmware in this repository is built against the libraries below. They are
not copied into this repository: PlatformIO downloads them when you build.
Each keeps its own licence, listed here.

| Library | Used for | Licence |
|---|---|---|
| [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) | Driving the screen | FreeBSD (2 clause BSD) |
| [WiFiManager](https://github.com/tzapu/WiFiManager) | The first run WiFi setup page | MIT |
| [ArduinoJson](https://github.com/bblanchon/ArduinoJson) | Reading the weather replies | MIT |
| [XPT2046_Touchscreen](https://github.com/PaulStoffregen/XPT2046_Touchscreen) | The touch panel on the 2.8 inch board | MIT |
| [ESPAsyncWebServer](https://github.com/esphome/ESPAsyncWebServer) | The settings page the clock hosts | **LGPL-3.0** |
| [AsyncTCP](https://github.com/esphome/AsyncTCP) | Underneath that web server | **LGPL-3.0** |

Full texts of the two GNU licences are in the `licenses` folder. The LGPL is
written as a set of changes to the GPL rather than as a standalone licence,
which is why both are there.

The clock also uses two free internet services, neither of which needs an
account or a key:

- [Open-Meteo](https://open-meteo.com/) for the forecast
- [Zippopotam.us](https://zippopotam.us/) for turning a postcode into a position

---

## What the LGPL parts mean in practice

The settings page the clock hosts is built on a web server library under the
LGPL-3.0. That licence is deliberately written so it can be combined with code
under any other licence, including closed source, so it does **not** force this
project's own code to be LGPL. MIT is fine and is what this project uses.

What it does ask for, once you hand someone a ready to flash file rather than
just source code, is that they are able to swap that library for their own
version and rebuild. In practice that means:

1. **Say that the library is used and is under the LGPL.** That is what the
   table above is for.
2. **Include a copy of the licence.** That is the `licenses` folder.
3. **Make the source available**, so anyone can change the library and build
   their own firmware.

Publishing this whole repository satisfies the third, which is the substantive
one. Anyone can clone it, point PlatformIO at a modified copy of the library,
and build a replacement for any of the files handed out from the flashing page.

So there is nothing extra to do beyond keeping the source public alongside the
binaries. It is only worth understanding because it would stop being true if
the firmware were ever shipped without the source.

---

*This is a plain English summary written to be useful, not legal advice.*
