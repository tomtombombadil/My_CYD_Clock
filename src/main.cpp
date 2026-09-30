// main.cpp
// My CYD Clock
//
// Start up order:
//   1. Read the saved settings.
//   2. Wake the screen and the touch panel.
//   3. Join WiFi. If that is not possible, open the setup network instead.
//   4. Start the time sync, the weather task and the settings web page.
//   5. Show the clock.

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <ESPmDNS.h>
#include <time.h>

#include "config.h"
#include "settings.h"
#include "display.h"
#include "weather.h"
#include "alarms.h"
#include "webui.h"
#include "logbuf.h"
#include "touch.h"

// The touch panel has its own SPI bus, separate from the display.

static bool     fingerDown     = false;
static uint32_t touchStartMs   = 0;
static bool     longPressDone  = false;
static int      touchX = -1, touchY = -1;   // where the screen was last touched

static uint32_t lastWifiOkMs   = 0;
static uint32_t lastRejoinMs   = 0;
static int      lastMinuteSeen = -1;

// ---------------------------------------------------------------------------
// WiFi
// ---------------------------------------------------------------------------

// Three small changes to the setup page, made by adding a script to it rather
// than editing the library.
//
// The first matters most. The library's front page is a menu, and choosing
// WiFi from it loads a page that scans for networks before it will draw, which
// it does with everything else stopped. That is the pause you get after
// tapping the button, with the button going grey and coming back while
// nothing appears to happen. Going straight to the WiFi page instead means
// there is one wait, at the moment you expect a page to be loading, rather
// than a quick page followed by a stall.
//
// The second shows the password as it is typed. This is a clock on a home
// network, and typing a long password blind onto a phone is a needless way to
// get it wrong.
//
// The third renames the button. It does not just save the network, it
// connects to it.
// The password box is called p on that page and its Show Password tick box is
// called showpass. The library's own handler decides which way to flip by
// looking at what the box is showing at that moment, so showing the password
// without also ticking the box left the two disagreeing, and the tick box then
// worked backwards. Both are set together here.
static const char PORTAL_HEAD[] =
  "<script>"
  "if(location.pathname=='/'||location.pathname==''){location.replace('/wifi');}"
  "document.addEventListener('DOMContentLoaded',function(){"
  "var p=document.getElementById('p');"
  "if(p){p.type='text';p.setAttribute('autocapitalize','none');"
  "p.setAttribute('autocorrect','off');p.setAttribute('spellcheck','false');}"
  "var s=document.getElementById('showpass');if(s){s.checked=true;}"
  "document.querySelectorAll('button,input[type=submit]').forEach(function(b){"
  "var t=(b.textContent||b.value||'').trim();"
  "if(t=='Save'){if(b.tagName=='INPUT')b.value='Connect';else b.textContent='Connect';}"
  "});});</script>";

// Turns a connection result into something worth reading on a small screen.
static String joinFailureText(uint8_t result) {
  switch (result) {
    case WL_NO_SSID_AVAIL:   return "That network was not found";
    case WL_CONNECT_FAILED:  return "The password was not accepted";
    case WL_CONNECTION_LOST: return "The connection dropped";
    default:                 return "Could not connect";
  }
}

// Puts up the setup network and keeps the screen updated while it waits.
//
// WiFiManager can run this for us and simply block until it is done. Running
// the loop ourselves costs a few more lines but means the display can keep
// showing what is going on, in particular whether a phone has actually joined
// the network yet, which is the first thing you want to know when the setup
// page has not appeared.
static bool runSetupPortal() {
  WiFiManager manager;
  manager.setTitle(FW_NAME);
  manager.setHostname(deviceHostname());
  manager.setDarkMode(true);
  manager.setCustomHeadElement(PORTAL_HEAD);
  manager.setCaptivePortalEnable(true);   // answer every address, so phones offer the page
  manager.setAPClientCheck(true);         // do not time out while a phone is still connected
  manager.setConfigPortalBlocking(false); // we run the waiting loop ourselves
  manager.setSaveConfigCallback([]() {
    uiMessage("Connected", "Saving your network...", "", "", TFT_GREEN);
  });

  manager.startConfigPortal(SETUP_AP_NAME);

  String ssid    = SETUP_AP_NAME;
  String address = "http://" + WiFi.softAPIP().toString();
  logLine("Setup network " + ssid + " is up at " + address);

  // With a network already saved, give up after three minutes and have another
  // go at it, in case it was only briefly away. With nothing saved there is
  // nothing to go back to, so wait for as long as it takes.
  uint32_t limit   = cfg.wifiSsid.length() ? 180000UL : 0;
  uint32_t started = millis();
  int      shownClients = -1;
  uint32_t lastPaint    = 0;
  uint8_t  lastResult   = WL_IDLE_STATUS;

  while (true) {
    if (manager.process()) break;
    if (WiFi.status() == WL_CONNECTED) break;

    // Say so when an attempt has been made and did not work, rather than
    // dropping back to the waiting screen with no explanation.
    uint8_t result = manager.getLastConxResult();
    if (result != lastResult) {
      lastResult = result;
      if (result != WL_IDLE_STATUS && result != WL_CONNECTED) {
        String why = joinFailureText(result);
        logLine("Setup: " + why);
        uiMessage("Could not join", "that network", why,
                  "Try again from your phone", TFT_RED);
        delay(4000);
        shownClients = -1;              // make sure the portal screen comes back
      }
    }

    int clients = WiFi.softAPgetStationNum();
    if (clients != shownClients || millis() - lastPaint > 5000) {
      if (clients != shownClients) {
        logLine(clients > 0 ? "A phone joined the setup network"
                            : "No phone connected to the setup network");
      }
      shownClients = clients;
      lastPaint    = millis();
      uiPortalScreen(ssid, address, clients);
    }

    if (limit != 0 && millis() - started > limit) {
      logLine("Setup page timed out, trying the saved network again");
      break;
    }
    delay(20);
  }

  bool connected = (WiFi.status() == WL_CONNECTED);

  // Write the network down straight away, before anything else can go wrong.
  // Losing it here is what used to send the clock round the setup loop again.
  if (connected) {
    cfg.wifiSsid = WiFi.SSID();
    cfg.wifiPass = WiFi.psk();
    settingsSave();
    logLine("Setup finished, joined " + cfg.wifiSsid + " and saved it");
  }

  // Only ask the library to close the portal if it has not already closed it
  // itself. It does that on its own the moment a connection succeeds, and it
  // does not cope with being closed twice: the second time it uses a web
  // server it has already thrown away, which crashes the chip.
  if (manager.getConfigPortalActive()) manager.stopConfigPortal();

  return connected;
}

// Gets the clock onto a network, one way or another.
static bool joinWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(deviceHostname().c_str());   // has to be set before connecting
  WiFi.setAutoReconnect(true);

  // First try whatever network was saved last time.
  if (cfg.wifiSsid.length() > 0) {
    uiMessage("Connecting to WiFi", cfg.wifiSsid, "", "", TFT_WHITE);
    WiFi.begin(cfg.wifiSsid.c_str(), cfg.wifiPass.c_str());
    uint32_t started = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - started < 15000) {
      delay(250);
    }
    if (WiFi.status() == WL_CONNECTED) return true;
  }

  // No network of ours. Clear the chip's own copy as well before going any
  // further.
  //
  // The chip remembers the last network it used, quite separately from our
  // settings. Left in place it would quietly reconnect on its own and the
  // setup page would never appear, which makes the whole first run experience
  // impossible to test: a factory reset would look like it had worked and then
  // the clock would silently rejoin the old network.
  //
  // Having no network of our own means either this clock has never been set
  // up, or it has just been reset. Either way the chip's copy is not wanted.
  if (cfg.wifiSsid.length() == 0) {
    logLine("No network saved, clearing the copy the chip keeps as well");
    WiFi.disconnect(true, true);
    delay(200);
  }

  // Put up our own open network so the user can point us at a real one.
  bool joined = runSetupPortal();
  if (!joined) {
    logLine("Setup page closed without joining a network");
    return false;
  }

  // Restart now that the network is saved.
  //
  // The setup page runs its own web server on port 80, and the chip does not
  // reliably hand that port back when it is shut down. The library says as
  // much in its own source. Carrying on regardless leaves the settings page
  // with nowhere to listen, so the clock answers a ping but nothing else.
  //
  // Starting again is the clean way out. The network has already been written
  // down, so the next start up connects to it directly, the setup page never
  // runs, and port 80 is free for the settings page.
  logLine("Network saved, restarting so the settings page can use port 80");
  uiMessage("Connected to", cfg.wifiSsid, "Restarting to finish setup", "",
            TFT_GREEN);
  delay(2500);
  ESP.restart();
  return true;                        // never reached
}

// ---------------------------------------------------------------------------
// Touch
// ---------------------------------------------------------------------------

// Wipes everything and starts over, as though the board had just been flashed.
static void factoryReset() {
  logLine("Factory reset asked for on the screen");
  uiMessage("Erasing everything", "", "The clock will restart in a moment", "",
            TFT_RED);
  settingsFactoryReset();
  // The chip keeps its own copy of the network name and password, separate
  // from our settings, so that has to go as well.
  WiFi.disconnect(true, true);
  delay(800);
  ESP.restart();
}

static void onShortTap() {
  if (alarmIsActive()) {
    alarmAcknowledge();
    uiSetScreen(SCREEN_CLOCK);
    return;
  }

  ScreenId screen = uiCurrentScreen();
  if (screen == SCREEN_STATUS || screen == SCREEN_RESET_CONFIRM) {
    switch (uiHitTest(touchX, touchY)) {
      case TOUCH_RESET_BUTTON: uiSetScreen(SCREEN_RESET_CONFIRM); return;
      case TOUCH_RESET_ERASE:  factoryReset();                    return;
      default:                 uiSetScreen(SCREEN_CLOCK);         return;
    }
  }

  uiNextScreen();
}

static void onLongPress() {
  if (alarmIsActive()) {
    alarmAcknowledge();
    uiSetScreen(SCREEN_CLOCK);
    return;
  }
  uiSetScreen(SCREEN_STATUS);
}

static void handleTouch() {
  bool down = touchIsDown();

  if (down && !fingerDown) {
    fingerDown    = true;
    touchStartMs  = millis();
    longPressDone = false;
    touchGetPoint(touchX, touchY);
  } else if (down && fingerDown && !longPressDone &&
             (millis() - touchStartMs >= LONG_PRESS_MS)) {
    longPressDone = true;
    onLongPress();
  } else if (!down && fingerDown) {
    fingerDown = false;
    if (!longPressDone) onShortTap();
  }
}

// ---------------------------------------------------------------------------
// Start up
// ---------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println(FW_NAME " " FW_VERSION);

  logBegin();
  logLine(String(FW_NAME) + " " + FW_VERSION + " starting up, built for the "
          BOARD_NAME " board");
  logLine(logRestartSummary());

  settingsLoad();

  uiBegin();
  logLine("Screen is " + String(tft.width()) + " by " + String(tft.height()));
  alarmsBegin();

  touchBegin();

  if (!joinWifi()) {
    uiMessage("Could not join WiFi", "Restarting...", "", "", TFT_RED);
    delay(3000);
    ESP.restart();
  }
  lastWifiOkMs = millis();
  logLine("Joined " + WiFi.SSID() + " with address " + WiFi.localIP().toString());

  // Keep the radio awake. Left to itself the chip dozes between the access
  // point's beacons to save power, and replies that arrive while it is asleep
  // wait for the next wake up. On a mains powered clock the power saving is
  // worth nothing, and the delays it causes are the usual reason a request
  // that should take a fraction of a second times out instead.
  WiFi.setSleep(false);

  uiMessage("Getting the time", "", cfg.ntpServer, "", TFT_YELLOW);
  configTzTime(cfg.timeZone.c_str(), cfg.ntpServer.c_str(),
               "pool.ntp.org", "time.google.com");

  logLine("Time server set to " + cfg.ntpServer + ", zone " + cfg.timeZoneName);

  // Let the clock answer to a name as well as an address, so you do not have
  // to know the address to reach the settings page.
  String hostname = deviceHostname();
  if (MDNS.begin(hostname.c_str())) {
    MDNS.addService("http", "tcp", 80);
    logLine("Also reachable at http://" + hostname + ".local");
  } else {
    logLine("Could not register the name " + hostname + " on the network");
  }

  weatherBegin();
  webBegin();
  logLine("Settings page is up");

  String ipUrl   = "http://" + WiFi.localIP().toString();
  String nameUrl = "http://" + deviceHostname() + ".local";
  uiConfigureScreen(ipUrl, nameUrl);
  Serial.println("Settings page: " + ipUrl + "  or  " + nameUrl);
  delay(6000);

  uiSetScreen(SCREEN_CLOCK);
  uiRedraw();
}

// ---------------------------------------------------------------------------
// Main loop
// ---------------------------------------------------------------------------

void loop() {
  struct tm timeNow;
  bool timeValid = getLocalTime(&timeNow, 5);

  handleTouch();
  alarmsTick(&timeNow, timeValid);
  uiTick(&timeNow, timeValid);
  webTick();

  // Once a minute, check whether the screen should dim for the evening.
  if (timeValid && timeNow.tm_min != lastMinuteSeen) {
    lastMinuteSeen = timeNow.tm_min;
    uiApplyAutoBrightness(timeNow.tm_hour * 60 + timeNow.tm_min);
  }

  // WiFi recovery. Restarting the whole clock every time the network hiccups
  // is far too heavy handed, and it is why the display would jump back to the
  // start up screen on its own. Now it quietly asks to rejoin, tries again
  // every half minute, and only restarts if the network has been gone for ten
  // minutes, which means something is genuinely wrong.
  if (WiFi.status() == WL_CONNECTED) {
    lastWifiOkMs  = millis();
    lastRejoinMs  = 0;
  } else {
    uint32_t downFor = millis() - lastWifiOkMs;
    if (lastRejoinMs == 0 || millis() - lastRejoinMs > 30000UL) {
      lastRejoinMs = millis();
      logLine("WiFi is not connected, trying to rejoin. Down for " +
              String(downFor / 1000UL) + " seconds");
      WiFi.disconnect();
      if (cfg.wifiSsid.length()) WiFi.begin(cfg.wifiSsid.c_str(), cfg.wifiPass.c_str());
      else                       WiFi.reconnect();
    }
    if (downFor > 600000UL) {
      logLine("WiFi has been gone for ten minutes, restarting the clock");
      uiMessage("WiFi lost", "Restarting...", "", "", TFT_RED);
      delay(2000);
      ESP.restart();
    }
  }

  logTick();
  delay(10);
}
