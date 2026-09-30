# My CYD Clock

A clock for the Cheap Yellow Display. It defaults to a retro look imitating seven segment
numerals, shows the weather when you tap it, and hosts its own settings page on
your network. No account, no cloud service, no app. It uses NIST for NTP time, so you
never have to set it.

<!-- Drop a photo of your clock in here. Put the file somewhere like docs/photo.jpg
     and change the line below to point at it. A photo does more for a project
     page than any amount of description. -->
<!-- ![My CYD Clock](docs/photo.jpg) -->

---

## Install it without building anything

**[Open the flashing page](https://tomtombombadil.github.io/My_CYD_Clock/)**,
plug the board into your computer, and click the button for your board. It
writes the firmware over the USB cable and takes about 2 minutes.

Make sure to use Chrome or Edge browser to flash your CYD. Firefox and Safari cannot talk to a USB
serial port, and neither can phones.

---

## Boards it runs on

| Board | Screen | Build name |
|---|---|---|
| 2.8 inch, ESP32-2432S028R | 240 x 320 | `cyd28` |
| 3.2 inch, ESP32-2432S032R | 240 x 320 | `cyd32` |
| 3.5 inch, ESP32-3248S035R | 320 x 480 | `cyd35` |

The 4.0 inch board built on the same panel as the 3.5 inch uses `cyd35` as
well, tested and working.

These are the resistive touch versions. The capacitive ones, with a C on the
end of the model number, use a different touch chip and are not supported.

The model number is printed on the back of the board.

---

## What it does

- **The time, as large as the screen allows.** The numerals are drawn rather
  than taken from a font, so they size themselves to whatever screen they find
  and to whether you have seconds and the date switched on.
- **Five clock faces.** Seven segment LED, bold sans, bold serif, typewriter
  and italic. Any text and background colour.
- **Weather on a tap.** Current conditions, the next four hours, and a three to
  ten day forecast. Tap through them, tap again for the clock.
- **A settings page** hit the clock's IP in your browser to configure the clock:
  colours, clock face, time zone, weather location, alarms, brightness.
- **Three alarms**, each with its own days, and separate switches for a tone, a
  flashing LED and a flashing screen.
- **Dims after sunset**, using the real sunrise and sunset times for your
  location.
- **Initial Setup Requires A Device That Can Connect To The Clock's WiFi.**
  When you first flash the CYD with the clock firmware, it will boot and create
  its own WiFi SSID called My_CYD_Clock. Connect to it with your laptop or phone
  and you'll be taken to the setup page, where you select the WiFi SSID that you
  want the clock to connect to, and enter the SSID's password. The CYD will then
  connect to that SSID (disabling the My_CYD_Clock SSID). It will briefly show its
  new IP address so you can go to that in a browser and configure the clock the way
  you like.

---

## Setting it up the first time

1. The clock looks for a network it has used before. On a fresh flash it won't know
   any wifi credentials, so it will make it's own ad hoc wifi network for you to join.
2. The screen tells you to join **`My_CYD_Clock`** on your phone. It is open,
   with no password.
3. A setup page should open by itself. If it does not, browse to the address on
   the CYD screen. Pick your WiFi network, type the password, press Connect.
4. The clock restarts and shows its new address on the WiFi network you entered.
5. Go to that address in your browser and you'll get the Settings page for the CYD.

Touch and hold the screen (long press) at any time to see that address again.

If a setup page does not open on its own, turning mobile data off usually fixes
it. Your phone may notice that the CYD's ad hoc network has no internet and
default back to your mobile connection.

---

## Using the screen

| Action | What happens |
|---|---|
| Tap | Next screen - cycles through the available/enabled screens, clock & weather|
| Touch and hold | Status page, with the address for the settings page and a factory reset button |
| Tap during an alarm | Cancels the alarm |

Weather screens return to the clock after a minute, the status page after 25
seconds.

---

## The settings page

Browse to the address the clock shows you. It is also reachable by name, as
`http://my_cyd_clock_xx.local`, where the last two characters come from the
board's own hardware address so two clocks on one network never collide.

**WiFi** — change networks, or forget the current one and start over.

**Network Time** — which time server to use, `time.nist.gov` by default, and
your time zone from a list. Daylight saving switches on its own.

**Time Display** — 12 or 24 hour, seconds, AM and PM, blinking colon, the date.
Clock face, the width of the dark line between segments, whether unlit segments
show faintly. Text and background colour, by picker, by code or from presets.
Brightness, night brightness, dim after sunset, and invert colours for panels
that show everything backwards.

**Weather** — postcode and country, Fahrenheit or Celsius, forecast length, and
which weather screens tapping cycles through.

**Alarms** — three, each with a time, days of the week, and switches for tone,
LED and screen. Each has a Test button.

There is also an activity log you can read or download, which is the first
place to look if something is not behaving.

---

## Building it yourself

You do not need to build anything to use this. This is for changing it.

**[FLASHING.md](FLASHING.md)** walks through it for someone who has not used
VS Code much, including how to pick your board.

The short version: open the folder in VS Code with the PlatformIO extension,
click the ant head icon in the left bar, find your board under Project Tasks,
and click Upload underneath it.

Two build settings are deliberate and worth leaving alone:

- The platform is pinned to `espressif32@6.9.0`. Newer versions install Arduino
  core 3.x, which the web server library does not compile against.
- The partition layout is `huge_app.csv`. The default gives the program about
  1.3 MB and this build is about 1.2 MB, which is too close for comfort.

Every build also writes a single ready to flash file into `docs/firmware`, which
is what the flashing page hands out. **[GITHUB-PAGES.md](GITHUB-PAGES.md)**
explains how to host that page from your own copy of this repository.

---

## How it works

**Time** comes from your chosen NTP time server. It defaults to time.nist.gov,
with `pool.ntp.org` and `time.google.com` as backups.

**Weather** comes from [Open-Meteo](https://open-meteo.com/), which is free and
needs no account or key, so the clock works the moment you type a postcode. The
postcode is turned into a position once by
[Zippopotam](https://zippopotam.us/) and the result is kept, so that lookup only
happens again when you change it.

The forecast is fetched **only when it is going to be used**: once at startup so
the screens are not empty, and again whenever you tap through to a weather
screen. A reading under thirty seconds old is reused. If dim after sunset is on,
it also fetches twice a day, because sunrise and sunset times only arrive with a
forecast. There is no polling on a timer. This keeps pointless network traffic
to a minimum and doesn't needlessly spam the free weather server.

---

## Hardware notes

**The seven segment numerals are drawn, not typed.** Each is seven tapered bars
that tile into one outline with chamfered corners, the same layout as a real LED
module. The dark line between segments is made by tracing the angled ends only,
so widening it never thins the bars.

**No flicker.** Every numeral has its own box and only boxes whose contents
changed get repainted. The screen is wiped in full only when the layout moves.

**The LED on the back** is on pins 4, 16 and 17, wired so that pulling a pin low
turns that colour on, which is backwards from what you would expect.

**The speaker** goes on the two pin header marked SPEAK, on pin 26. The tone is
a square wave, so it buzzes rather than sings, which is what you want from an
alarm.

**The backlight and the speaker are on different PWM timers** on purpose.
Channels 0 and 1 share a timer, 2 and 3 share another. The backlight is on 0 and
the speaker on 2, so changing the alarm tone cannot change the screen
brightness.

**Touch wiring differs between boards.** On the 2.8 inch the touch panel has
four wires of its own. On the 3.2 and 3.5 inch it shares the display's wires, so
the display library has to read it instead. `src/touch.cpp` covers both behind
one set of functions.

---

## What is in the repository

| File | What is in it |
|---|---|
| `platformio.ini` | Build settings and the three board definitions |
| `merge_firmware.py` | Build step that makes one ready to flash file per board |
| `src/boards.h` | Everything that differs between the three boards |
| `src/config.h` | Fixed values shared by all of them |
| `src/touch.h/.cpp` | Reading the touch panel on either wiring style |
| `src/display.h/.cpp` | All drawing: clock, weather, status, alarm screens |
| `src/settings.h/.cpp` | Every setting, and keeping them across a reboot |
| `src/weather.h/.cpp` | The background task that fetches the forecast |
| `src/alarms.h/.cpp` | Alarm times, the tone, the LED and the screen flash |
| `src/webui.h/.cpp` | The web server and its data endpoints |
| `src/webpage.h` | The settings page itself |
| `src/logbuf.h/.cpp` | The activity log, which survives a restart |
| `src/main.cpp` | Startup, WiFi, touch handling, main loop |
| `docs/` | The web flashing page and the files it hands out |

---

## Things worth knowing if you change it

**Touch positions are approximate.** Only one thing in the firmware cares
*where* you touched rather than just that you did: the factory reset button.
The panel reports rough numbers rather than pixels and the usable range varies
between boards, so the figures are per board in `src/boards.h` with notes on
adjusting them. Every touch is printed to the serial monitor to make that easy.

**The weather icons are drawn with lines and circles** rather than stored
pictures, so they stay sharp at any size and cost no program space.
`drawWeatherIcon` in `display.cpp` is the only place to change them.

**Three alarms** is set by `ALARM_COUNT` in `settings.h`. Raising it means
adding matching rows to the settings page too.

**The screens size themselves** from the actual screen dimensions rather than
fixed pixel positions, so anything you add should do the same. `acrossX()` and
`downY()` in `display.cpp` are the helpers for it.

---

## Licence

[MIT](LICENSE) for the code in this repository.

The libraries it builds against keep their own licences, and two of them are
LGPL-3.0, which carries conditions once you hand out a ready to flash file.
**[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md)** lists them all and explains
what those conditions actually mean. Keeping this repository public alongside
the binaries is the whole of it.

---

## Version history

**[CHANGELOG.md](CHANGELOG.md)** has every change with the reasoning behind it.
