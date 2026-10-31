// =============================================================================
//  Mayavi -- BLE multi-device emulation + keystroke injection for ESP32-S3
//
//    no display, no buttons: the dashboard is the UI.
//      access point  Mayavi-XXXX (open)   ->  http://192.168.4.1
//      serial CLI    115200 baud
//
//  Use it only on hardware you own or have written permission to test.
// =============================================================================

#include <Arduino.h>
#include <WiFi.h>

#include "config.h"
#include "console.h"
#include "emul.h"
#include "payload.h"
#include "settings.h"
#include "web.h"

static void setLed(uint8_t r, uint8_t g, uint8_t b) {
  neopixelWrite(STATUS_LED_PIN, r, g, b);
}

static void startAccessPoint() {
  const Settings &c = settings::get();

  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);

  // Empty password == open network; pass NULL so the driver configures it
  // without a passphrase instead of with a zero-length one.
  const char *pass = c.apPass[0] ? c.apPass : nullptr;

  if (!WiFi.softAP(c.apSsid, pass)) {
    Serial.println("[net] softAP FAILED");
    return;
  }

  Serial.printf("[net] AP \"%s\" (%s)  http://%s/\n", c.apSsid,
                pass ? "WPA2" : "open", WiFi.softAPIP().toString().c_str());
}

void setup() {
  Serial.begin(115200);
  delay(1200);  // let USB CDC enumerate before the first print

  Serial.println();
  Serial.println("=================================");
  Serial.println("  Mayavi " MAYAVI_VERSION);
  Serial.println("=================================");
  Serial.printf("[sys] %s  rev %u  %u MB flash  %u MB PSRAM\n",
                ESP.getChipModel(), (unsigned)ESP.getChipRevision(),
                (unsigned)(ESP.getFlashChipSize() / (1024 * 1024)),
                (unsigned)(ESP.getPsramSize() / (1024 * 1024)));

  setLed(0, 0, 32);  // blue: booting

  settings::begin();
  startAccessPoint();

  payload::begin();
  emul::begin();
  webBegin();
  consoleBegin();

  setLed(0, 0, 12);
  Serial.printf("[sys] dashboard: http://%s/\n", WiFi.softAPIP().toString().c_str());
}

void loop() {
  webLoop();
  consoleLoop();
  emul::loop();
  payload::loop();

  // Status LED, refreshed a few times a second.
  static uint32_t lastLed = 0;
  static uint8_t phase = 0;
  const uint32_t now = millis();
  if (now - lastLed > 120) {
    lastLed = now;
    phase ^= 1;

    if (emul::ready()) {
      setLed(0, 32, 0);                       // green: HID ready
    } else if (emul::connected()) {
      setLed(0, 24, 24);                      // cyan: link up, securing
    } else if (emul::advertising()) {
      setLed(phase ? 0 : 6, 0, phase ? 20 : 8);  // blue: advertising
    } else {
      setLed(6, 4, 0);                        // amber-dim: idle
    }
  }

  delay(2);
}
