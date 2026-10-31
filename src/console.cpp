#include "console.h"

#include <WiFi.h>

#include "config.h"
#include "emul.h"
#include "payload.h"
#include "settings.h"

static bool s_scriptMode = false;
static String s_script;

// ---------------------------------------------------------------------------
// menus
// ---------------------------------------------------------------------------
static void printHelp() {
  Serial.println(
      "\n  Mayavi " MAYAVI_VERSION " -- commands\n"
      "  ---- discovery ------------------------------------------------\n"
      "  1 / start [sec]     start BLE discovery advertising\n"
      "  0 / stop            stop advertising / disconnect the host\n"
      "  2 / status          connection, security, payload state\n"
      "  3 / profiles        list emulated device profiles\n"
      "  profile <id>        switch profile (e.g. profile apple-tv)\n"
      "  name <text>         change the advertised BLE name\n"
      "  appearance <hex>    raw appearance code (e.g. 0x03C1)\n"
      "  duration <sec>      discovery timeout, 5..60\n"
      "  mode swift|beacon   Microsoft Swift Pair or plain beacon\n"
      "  forget              clear stored Bluetooth bonds\n"
      "  ---- payloads -------------------------------------------------\n"
      "  4 / hid             payload menu\n"
      "  5 / hello           Notepad + Hello test payload\n"
      "  payload begin       enter script mode, finish with END\n"
      "  payload show        print the stored script\n"
      "  payload run         execute the stored script\n"
      "  abort               stop the payload and release all keys\n"
      "  ---- system ---------------------------------------------------\n"
      "  wifi                show the dashboard access point\n"
      "  wifi open           drop the password (open network), reboot\n"
      "  wifi set <s> <p>    change SSID and password, then reboot\n"
      "  reboot              restart the board\n");
}

static void printHidMenu() {
  Serial.println(
      "\n  HID payload menu\n"
      "  5 / hello           Notepad + Hello test payload\n"
      "  payload begin       enter a DuckyScript-style script, END to save\n"
      "  payload show        print the stored script\n"
      "  payload run         execute the stored script\n"
      "  abort               stop immediately\n"
      "\n  syntax: STRING/DELAY/ENTER/TAB/ESC/GUI r/CTRL SHIFT ESC/REM\n");
}

static void printProfiles() {
  Serial.println("\n   ID            APPEARANCE  PROFILE              ADVERTISED NAME");
  Serial.println("   ------------  ---------  -------------------  --------------------");
  for (int i = 0; i < emul::profileCount(); i++) {
    const ProfileDef *p = emul::profile(i);
    if (!p) continue;
    Serial.printf("   %-12s  0x%04X     %-19s  %s%s\n", p->id, p->appearance,
                  p->label, p->name,
                  (emul::currentProfile() == i) ? "  <-" : "");
  }
  Serial.println();
}

static void printStatus() {
  const Settings &c = settings::get();

  Serial.println();
  Serial.printf("  version    %s\n", MAYAVI_VERSION);
  Serial.printf("  ble state  %s\n", emul::stateText());
  Serial.printf("  profile    %s\n", emul::currentProfileId().c_str());
  Serial.printf("  name       %s\n", c.bleName);
  Serial.printf("  appearance 0x%04X\n", c.appearance);
  Serial.printf("  mode       %s\n", c.mode == ADV_SWIFT_PAIR ? "swift pair" : "beacon");
  Serial.printf("  bonds      %d\n", emul::bonds());
  if (emul::advertising()) {
    Serial.printf("  window     %lu s left\n", (unsigned long)emul::secondsLeft());
  }
  Serial.printf("  payload    %s (%d%%, %d/%d)\n", payload::state(),
                payload::progress(), payload::currentStep(), payload::steps());
  if (payload::countdownLeft()) {
    Serial.printf("  countdown  %lu s\n", (unsigned long)payload::countdownLeft());
  }
  if (payload::error().length()) {
    Serial.printf("  error      %s\n", payload::error().c_str());
  }
  Serial.printf("  ap         %s  http://%s\n", c.apSsid,
                WiFi.softAPIP().toString().c_str());
  Serial.printf("  heap       %u KB free\n", (unsigned)(ESP.getFreeHeap() / 1024));
  Serial.printf("  uptime     %lu s\n", (unsigned long)(millis() / 1000));
  Serial.println();
}

static void printWifi() {
  const Settings &c = settings::get();
  Serial.println();
  Serial.printf("  ssid       %s\n", c.apSsid);
  Serial.printf("  password   %s\n", c.apPass[0] ? c.apPass : "(open, no password)");
  Serial.printf("  address    http://%s/\n", WiFi.softAPIP().toString().c_str());
  Serial.println();
}

// ---------------------------------------------------------------------------
// commands
// ---------------------------------------------------------------------------
static void doStart(uint32_t seconds) {
  if (emul::connected()) {
    Serial.println("  a host is already connected -- send '0' to drop it first");
    return;
  }
  emul::start(seconds);
}

static void doRunPayload() {
  if (payload::active()) {
    Serial.println("  a payload is already running -- 'abort' first");
    return;
  }
  if (!payload::start()) {
    Serial.printf("  cannot run: %s\n", payload::error().c_str());
    return;
  }
  Serial.println("  payload queued, watch the target screen");
}

static void handlePayloadCommand(const String &rest) {
  String sub = rest;
  sub.trim();
  sub.toLowerCase();

  if (sub == "begin") {
    s_scriptMode = true;
    s_script = "";
    Serial.println(
        "\n  script mode. type one instruction per line, END to save.\n"
        "  STRING Hello / DELAY 500 / GUI r / CTRL SHIFT ESC / ENTER\n");
    return;
  }
  if (sub == "show") {
    Serial.println();
    Serial.println(payload::text());
    Serial.println();
    return;
  }
  if (sub == "run" || sub == "start") {
    doRunPayload();
    return;
  }
  if (sub == "stop" || sub == "abort") {
    payload::stop();
    return;
  }
  Serial.println("  usage: payload begin | show | run | stop");
}

void consoleBegin() {
  Serial.println();
  Serial.println("  type 'help' for the command list");
  Serial.println();
}

void consoleLoop() {
  if (!Serial.available()) return;

  String line = Serial.readStringUntil('\n');
  line.replace("\r", "");

  // -------- interactive script entry --------
  if (s_scriptMode) {
    line.trim();
    if (line.equalsIgnoreCase("END")) {
      s_scriptMode = false;
      if (payload::setText(s_script)) {
        Serial.printf("\n  script saved (%u bytes). 'payload run' to execute.\n\n",
                      (unsigned)s_script.length());
      } else {
        Serial.println("\n  script too large, discarded\n");
      }
      return;
    }
    if (line.length()) {
      s_script += line;
      s_script += '\n';
    }
    return;
  }

  line.trim();
  if (!line.length()) return;

  const int sp = line.indexOf(' ');
  const String cmd = (sp < 0) ? line : line.substring(0, sp);
  String rest = (sp < 0) ? String() : line.substring(sp + 1);
  rest.trim();

  if (cmd == "help" || cmd == "menu" || cmd == "?") {
    printHelp();
  } else if (cmd == "1" || cmd == "start") {
    doStart(rest.length() ? (uint32_t)rest.toInt() : 0);
  } else if (cmd == "0" || cmd == "stop") {
    emul::stop();
  } else if (cmd == "2" || cmd == "status") {
    printStatus();
  } else if (cmd == "3" || cmd == "profiles") {
    printProfiles();
  } else if (cmd == "4" || cmd == "hid") {
    printHidMenu();
  } else if (cmd == "5" || cmd == "hello") {
    payload::setText(payload::presetHello());
    doRunPayload();
  } else if (cmd == "profile") {
    if (!rest.length()) {
      Serial.println("  usage: profile <id>   (3 / profiles lists them)");
    } else if (!emul::setProfile(rest)) {
      Serial.println("  unknown profile");
    } else {
      Serial.printf("  profile -> %s (%s)\n", emul::currentProfileId().c_str(),
                    settings::get().bleName);
    }
  } else if (cmd == "name") {
    if (!rest.length()) {
      Serial.printf("  name is \"%s\"\n", settings::get().bleName);
    } else {
      emul::setName(rest.c_str());
      Serial.printf("  name -> \"%s\"\n", settings::get().bleName);
    }
  } else if (cmd == "appearance") {
    if (!rest.length()) {
      Serial.printf("  appearance is 0x%04X\n", settings::get().appearance);
    } else {
      String hex = rest;
      if (hex.startsWith("0x") || hex.startsWith("0X")) hex = hex.substring(2);
      emul::setAppearance((uint16_t)strtoul(hex.c_str(), nullptr, 16));
      Serial.printf("  appearance -> 0x%04X (custom profile)\n",
                    settings::get().appearance);
    }
  } else if (cmd == "duration") {
    const long v = rest.toInt();
    if (v >= DISCOVERY_MIN_SEC && v <= DISCOVERY_MAX_SEC) {
      settings::get().discoverySec = (uint16_t)v;
      settings::save();
      Serial.printf("  discovery window -> %ld s\n", v);
    } else {
      Serial.printf("  usage: duration %d..%d\n", DISCOVERY_MIN_SEC, DISCOVERY_MAX_SEC);
    }
  } else if (cmd == "mode") {
    String m = rest;
    m.toLowerCase();
    if (m == "swift") {
      emul::setMode(ADV_SWIFT_PAIR);
      Serial.println("  mode -> Microsoft Swift Pair");
    } else if (m == "beacon") {
      emul::setMode(ADV_BEACON);
      Serial.println("  mode -> standard beacon");
    } else {
      Serial.println("  usage: mode swift|beacon");
    }
  } else if (cmd == "forget") {
    emul::forgetBonds();
  } else if (cmd == "payload") {
    handlePayloadCommand(rest);
  } else if (cmd == "abort") {
    payload::stop();
  } else if (cmd == "wifi") {
    if (!rest.length()) {
      printWifi();
      return;
    }
    if (rest == "open") {
      Settings &c = settings::get();
      c.apPass[0] = '\0';
      settings::save();
      Serial.println("  access point is now open, rebooting...");
      delay(400);
      ESP.restart();
    }
    if (!rest.startsWith("set ")) {
      Serial.println("  usage: wifi | wifi open | wifi set <ssid> <password>");
      return;
    }
    const String args = rest.substring(4);
    const int s2 = args.indexOf(' ');
    if (s2 < 0 || s2 == 0) {
      Serial.println("  usage: wifi set <ssid> <password>   (empty password = open)");
      return;
    }
    String ssid = args.substring(0, s2);
    String pass = args.substring(s2 + 1);
    pass.trim();
    if (ssid.length() > 32 || (pass.length() > 0 && pass.length() < 8)) {
      Serial.println("  ssid max 32 bytes; password empty (open) or 8..63");
      return;
    }
    Settings &c = settings::get();
    strncpy(c.apSsid, ssid.c_str(), sizeof(c.apSsid) - 1);
    c.apSsid[sizeof(c.apSsid) - 1] = '\0';
    strncpy(c.apPass, pass.c_str(), sizeof(c.apPass) - 1);
    c.apPass[sizeof(c.apPass) - 1] = '\0';
    settings::save();
    Serial.println("  access point saved, rebooting...");
    delay(400);
    ESP.restart();
  } else if (cmd == "reboot") {
    Serial.println("  rebooting...");
    delay(200);
    ESP.restart();
  } else {
    Serial.printf("  unknown command: %s (try 'help')\n", cmd.c_str());
  }
}
