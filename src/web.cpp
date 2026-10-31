#include "web.h"

#include <LittleFS.h>
#include <WebServer.h>
#include <WiFi.h>

#include "config.h"
#include "emul.h"
#include "payload.h"
#include "settings.h"

static WebServer server(80);

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------
static String jsonEscape(const String &in) {
  String out;
  out.reserve(in.length() + 8);
  for (size_t i = 0; i < in.length(); i++) {
    const uint8_t c = (uint8_t)in[i];
    if (c == '"' || c == '\\') {
      out += '\\';
      out += (char)c;
    } else if (c == '\n') {
      out += "\\n";
    } else if (c == '\r') {
      // drop
    } else if (c < 0x20) {
      out += ' ';
    } else {
      out += (char)c;
    }
  }
  return out;
}

static const char *mimeFor(const String &path) {
  if (path.endsWith(".html")) return "text/html";
  if (path.endsWith(".js"))   return "application/javascript";
  if (path.endsWith(".css"))  return "text/css";
  if (path.endsWith(".json")) return "application/json";
  if (path.endsWith(".svg"))  return "image/svg+xml";
  if (path.endsWith(".png"))  return "image/png";
  return "text/plain";
}

static bool serveStatic(const String &path) {
  if (path.indexOf("..") >= 0) return false;  // no traversal
  if (!LittleFS.exists(path)) return false;

  File f = LittleFS.open(path, "r");
  if (!f) return false;

  server.streamFile(f, mimeFor(path));
  f.close();
  return true;
}

static void sendError(int code, const String &msg) {
  server.send(code, "application/json", "{\"error\":\"" + jsonEscape(msg) + "\"}");
}

static String arg(const char *name) {
  return server.hasArg(name) ? server.arg(name) : String();
}

// ---------------------------------------------------------------------------
// status
// ---------------------------------------------------------------------------
static void handleStatus() {
  Settings &c = settings::get();

  String j;
  j.reserve(700);
  j += "{\"version\":\"" MAYAVI_VERSION "\"";
  j += ",\"chip\":\"";  j += ESP.getChipModel();
  j += "\",\"ip\":\"";  j += WiFi.softAPIP().toString();
  j += "\",\"ssid\":\""; j += jsonEscape(c.apSsid);
  j += "\",\"open\":"; j += (c.apPass[0] ? "false" : "true");
  j += "\",\"uptime\":"; j += String((uint32_t)(millis() / 1000));
  j += ",\"heap\":";     j += String((uint32_t)ESP.getFreeHeap());

  j += ",\"ble\":{";
  j += "\"state\":\"";      j += emul::stateText();
  j += "\",\"advertising\":"; j += emul::advertising() ? "true" : "false";
  j += ",\"connected\":";    j += emul::connected() ? "true" : "false";
  j += ",\"ready\":";        j += emul::ready() ? "true" : "false";
  j += ",\"bonds\":";        j += String(emul::bonds());
  j += ",\"profile\":\"";    j += emul::currentProfileId();
  j += "\",\"name\":\"";     j += jsonEscape(c.bleName);
  j += "\",\"appearance\":\"0x";
  {
    char hex[8];
    snprintf(hex, sizeof(hex), "%04X", c.appearance);
    j += hex;
  }
  j += "\",\"mode\":\""; j += (c.mode == ADV_SWIFT_PAIR ? "swift" : "beacon");
  j += "\",\"left\":";   j += String(emul::secondsLeft());
  j += ",\"window\":";   j += String(c.discoverySec);
  j += "}";

  j += ",\"payload\":{";
  j += "\"state\":\"";     j += payload::state();
  j += "\",\"progress\":"; j += String(payload::progress());
  j += ",\"countdown\":";  j += String(payload::countdownLeft());
  j += ",\"steps\":";      j += String(payload::steps());
  j += ",\"current\":";    j += String(payload::currentStep());
  j += ",\"error\":\"";    j += jsonEscape(payload::error());
  j += "\"}";

  j += "}";
  server.send(200, "application/json", j);
}

// ---------------------------------------------------------------------------
// profiles
// ---------------------------------------------------------------------------
static void handleProfiles() {
  String j;
  j.reserve(1600);
  j += "{\"current\":\"";
  j += emul::currentProfileId();
  j += "\",\"profiles\":[";

  for (int i = 0; i < emul::profileCount(); i++) {
    const ProfileDef *p = emul::profile(i);
    if (!p) continue;
    if (i) j += ',';
    char hex[8];
    snprintf(hex, sizeof(hex), "0x%04X", p->appearance);
    j += "{\"id\":\"";     j += p->id;
    j += "\",\"label\":\""; j += p->label;
    j += "\",\"name\":\"";  j += jsonEscape(p->name);
    j += "\",\"appearance\":\""; j += hex;
    j += "\",\"hint\":\"";  j += p->hint;
    j += "\"}";
  }
  j += "]}";
  server.send(200, "application/json", j);
}

// ---------------------------------------------------------------------------
// emulation control
// ---------------------------------------------------------------------------
static void handleStart() {
  uint32_t sec = 0;
  if (server.hasArg("duration")) {
    const long v = arg("duration").toInt();
    if (v > 0) sec = (uint32_t)v;
  }
  if (!emul::start(sec)) {
    sendError(409, "a host is already connected");
    return;
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

static void handleStop() {
  emul::stop();
  server.send(200, "application/json", "{\"ok\":true}");
}

static void handleProfile() {
  const String id = arg("id");
  if (!id.length()) {
    sendError(400, "missing id");
    return;
  }
  if (!emul::setProfile(id)) {
    sendError(404, "unknown profile");
    return;
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

static void handleName() {
  String name = arg("name");
  name.trim();
  if (!name.length()) {
    sendError(400, "empty name");
    return;
  }
  if (name.length() > MAX_BLE_NAME) name = name.substring(0, MAX_BLE_NAME);
  emul::setName(name.c_str());
  server.send(200, "application/json", "{\"ok\":true}");
}

static void handleAppearance() {
  String hex = arg("appearance");
  hex.trim();
  if (hex.startsWith("0x") || hex.startsWith("0X")) hex = hex.substring(2);
  if (!hex.length()) {
    sendError(400, "missing appearance");
    return;
  }
  uint16_t value = 0;
  for (size_t i = 0; i < hex.length(); i++) {
    const char c = hex[i];
    int d = -1;
    if (c >= '0' && c <= '9') d = c - '0';
    else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
    if (d < 0) {
      sendError(400, "bad hex");
      return;
    }
    value = (uint16_t)((value << 4) | d);
  }
  emul::setAppearance(value);
  server.send(200, "application/json", "{\"ok\":true}");
}

static void handleMode() {
  const String m = arg("mode");
  if (m == "swift") emul::setMode(ADV_SWIFT_PAIR);
  else if (m == "beacon") emul::setMode(ADV_BEACON);
  else {
    sendError(400, "mode must be swift or beacon");
    return;
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

static void handleForget() {
  emul::forgetBonds();
  server.send(200, "application/json",
              "{\"ok\":true,\"bonds\":" + String(emul::bonds()) + "}");
}

static void handleDuration() {
  const long v = arg("seconds").toInt();
  if (v < DISCOVERY_MIN_SEC || v > DISCOVERY_MAX_SEC) {
    sendError(400, "seconds out of range");
    return;
  }
  settings::get().discoverySec = (uint16_t)v;
  settings::save();
  server.send(200, "application/json", "{\"ok\":true}");
}

// ---------------------------------------------------------------------------
// payload
// ---------------------------------------------------------------------------
static void handlePayloadGet() {
  String j;
  j.reserve(payload::text().length() + 200);
  j += "{\"text\":\""; j += jsonEscape(payload::text());
  j += "\",\"state\":\""; j += payload::state();
  j += "\",\"presets\":{\"hello\":\""; j += jsonEscape(payload::presetHello());
  j += "\"}}";
  server.send(200, "application/json", j);
}

static void handlePayloadSave() {
  if (!server.hasArg("text")) {
    sendError(400, "missing text");
    return;
  }
  if (!payload::setText(arg("text"))) {
    sendError(413, "script too large");
    return;
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

static void handlePayloadRun() {
  if (server.hasArg("text") && !payload::setText(arg("text"))) {
    sendError(413, "script too large");
    return;
  }

  uint32_t countdown = COUNTDOWN_MS;
  if (server.hasArg("countdown")) {
    const long ms = arg("countdown").toInt();
    if (ms >= 0) countdown = (uint32_t)ms;
  }

  if (!payload::start(countdown)) {
    const String err = payload::error();
    sendError(409, err.length() ? err : "payload already running");
    return;
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

static void handlePayloadStop() {
  payload::stop();
  server.send(200, "application/json", "{\"ok\":true}");
}

// ---------------------------------------------------------------------------
// settings
// ---------------------------------------------------------------------------
static void handleSettingsGet() {
  Settings &c = settings::get();
  String j;
  j.reserve(256);
  j += "{\"ssid\":\"";  j += jsonEscape(c.apSsid);
  j += "\",\"password\":\""; j += jsonEscape(c.apPass);
  j += "\",\"open\":"; j += (c.apPass[0] ? "false" : "true");
  j += "\",\"name\":\""; j += jsonEscape(c.bleName);
  j += "\",\"appearance\":\"0x";
  char hex[8];
  snprintf(hex, sizeof(hex), "%04X", c.appearance);
  j += hex;
  j += "\",\"mode\":\""; j += (c.mode == ADV_SWIFT_PAIR ? "swift" : "beacon");
  j += "\",\"window\":"; j += String(c.discoverySec);
  j += "}";
  server.send(200, "application/json", j);
}

static void handleSettingsPost() {
  Settings &c = settings::get();
  bool restart = false;

  if (server.hasArg("ssid")) {
    String ssid = arg("ssid");
    ssid.trim();
    if (ssid.length() >= 1 && ssid.length() <= 32) {
      strncpy(c.apSsid, ssid.c_str(), sizeof(c.apSsid) - 1);
      c.apSsid[sizeof(c.apSsid) - 1] = '\0';
      restart = true;
    }
  }
  if (server.hasArg("password")) {
    String pass = arg("password");
    // Empty = open network. Anything else must satisfy WPA2's 8..63 range.
    if (pass.length() == 0 || (pass.length() >= 8 && pass.length() <= 63)) {
      strncpy(c.apPass, pass.c_str(), sizeof(c.apPass) - 1);
      c.apPass[sizeof(c.apPass) - 1] = '\0';
      restart = true;
    }
  }
  if (server.hasArg("window")) {
    const long v = arg("window").toInt();
    if (v >= DISCOVERY_MIN_SEC && v <= DISCOVERY_MAX_SEC) {
      c.discoverySec = (uint16_t)v;
    }
  }
  settings::save();

  server.send(200, "application/json",
              String("{\"ok\":true,\"reboot\":") + (restart ? "true" : "false") + "}");

  if (restart) {
    Serial.println("[net] access point changed, rebooting");
    delay(600);
    ESP.restart();
  }
}

// ---------------------------------------------------------------------------
// routing
// ---------------------------------------------------------------------------
static void handleRoot() {
  if (!serveStatic("/index.html")) {
    server.send(500, "text/plain",
                "LittleFS has no index.html.\n"
                "Run:  pio run -e mayavi -t uploadfs\n");
  }
}

static void handleNotFound() {
  if (serveStatic(server.uri())) return;
  server.send(404, "text/plain", "not found\n");
}

void webBegin() {
  if (!LittleFS.begin(true)) {
    Serial.println("[fs] LittleFS mount FAILED");
  } else {
    Serial.printf("[fs] LittleFS ok, %u KB used of %u KB\n",
                  (unsigned)(LittleFS.usedBytes() / 1024),
                  (unsigned)(LittleFS.totalBytes() / 1024));
  }

  server.on("/", HTTP_GET, handleRoot);

  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/profiles", HTTP_GET, handleProfiles);

  server.on("/api/emul/start", HTTP_POST, handleStart);
  server.on("/api/emul/stop", HTTP_POST, handleStop);
  server.on("/api/emul/profile", HTTP_POST, handleProfile);
  server.on("/api/emul/name", HTTP_POST, handleName);
  server.on("/api/emul/appearance", HTTP_POST, handleAppearance);
  server.on("/api/emul/mode", HTTP_POST, handleMode);
  server.on("/api/emul/duration", HTTP_POST, handleDuration);
  server.on("/api/emul/forget", HTTP_POST, handleForget);

  server.on("/api/payload", HTTP_GET, handlePayloadGet);
  server.on("/api/payload", HTTP_POST, handlePayloadSave);
  server.on("/api/payload/run", HTTP_POST, handlePayloadRun);
  server.on("/api/payload/stop", HTTP_POST, handlePayloadStop);

  server.on("/api/settings", HTTP_GET, handleSettingsGet);
  server.on("/api/settings", HTTP_POST, handleSettingsPost);

  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("[web] listening on port 80");
}

void webLoop() { server.handleClient(); }
