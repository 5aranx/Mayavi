#include "payload.h"

#include <Preferences.h>
#include <vector>

#include "config.h"
#include "emul.h"
#include "hid.h"

namespace payload {

enum State : uint8_t { IDLE, COUNTDOWN, RUNNING, DONE, ABORTED, ERROR };

enum StepKind : uint8_t { STEP_STRING, STEP_DELAY, STEP_TAP };

struct Step {
  uint8_t kind;
  uint8_t mods;
  uint16_t usage;
  uint32_t ms;
  String text;
};

static String s_text;
static std::vector<Step> s_steps;
static State s_state = IDLE;
static size_t s_step = 0;
static size_t s_pos = 0;
static uint32_t s_nextAt = 0;
static uint32_t s_countdownAt = 0;
static uint16_t s_charDelay = CHAR_DELAY_MS;
static String s_error;
static Preferences s_prefs;

// ---------------------------------------------------------------------------
// Parsing
// ---------------------------------------------------------------------------

static bool addStep(const Step &st) {
  if (s_steps.size() >= MAX_PAYLOAD_STEPS) return false;
  s_steps.push_back(st);
  return true;
}

static bool parseTap(const String &line, Step &out, String &err) {
  uint8_t mods = 0;
  uint16_t usage = 0;
  bool haveKey = false;

  int i = 0;
  const int n = line.length();

  while (i < n) {
    while (i < n && line[i] == ' ') i++;
    int j = i;
    while (j < n && line[j] != ' ') j++;
    if (j <= i) break;

    const String tok = line.substring(i, j);
    i = j;

    uint8_t m = 0;
    uint16_t u = 0;

    if (haveKey) {
      err = "unexpected \"" + tok + "\" after key";
      return false;
    }

    if (hid::namedKey(tok, m, u)) {
      if (u == 0) {  // modifier
        mods |= m;
        continue;
      }
      usage = u;
      haveKey = true;
      continue;
    }

    if (tok.length() == 1) {
      if (!hid::charUsage(tok[0], m, u)) {
        err = "cannot type \"" + tok + "\"";
        return false;
      }
      mods |= m;
      usage = u;
      haveKey = true;
      continue;
    }

    err = "unknown key \"" + tok + "\"";
    return false;
  }

  out.kind = STEP_TAP;
  out.mods = mods;
  out.usage = usage;
  out.ms = 0;
  return true;
}

static bool parse(const String &src, String &err) {
  s_steps.clear();
  s_steps.reserve(32);
  s_charDelay = CHAR_DELAY_MS;

  int pos = 0;
  int lineNo = 0;

  while (pos <= (int)src.length()) {
    int nl = src.indexOf('\n', pos);
    String line = (nl < 0) ? src.substring(pos) : src.substring(pos, nl);
    pos = (nl < 0) ? (int)src.length() + 1 : nl + 1;
    lineNo++;

    line.replace("\r", "");
    if (!line.length()) continue;

    String upper = line;
    upper.toUpperCase();

    // Comments and blank lines
    if (upper.startsWith("REM ") || upper == "REM" || line.startsWith("#") ||
        line.startsWith("//")) {
      continue;
    }

    Step st = {};
    st.kind = STEP_TAP;

    if (upper.startsWith("STRING ") || upper == "STRING") {
      st.kind = STEP_STRING;
      st.text = (line.length() > 7) ? line.substring(7) : String();
      if (!addStep(st)) { err = "too many instructions"; return false; }
      continue;
    }

    if (upper.startsWith("DELAY ") || upper == "DELAY") {
      st.kind = STEP_DELAY;
      st.ms = (line.length() > 6) ? (uint32_t)line.substring(6).toInt() : 0;
      if (!addStep(st)) { err = "too many instructions"; return false; }
      continue;
    }

    if (upper.startsWith("DEFAULTDELAY ") || upper.startsWith("DEFAULT_DELAY ")) {
      const int sp = line.indexOf(' ');
      s_charDelay = (uint16_t)line.substring(sp + 1).toInt();
      if (s_charDelay < 2) s_charDelay = 2;
      if (s_charDelay > 500) s_charDelay = 500;
      continue;
    }

    String perr;
    if (!parseTap(line, st, perr)) {
      err = "line " + String(lineNo) + ": " + perr;
      return false;
    }
    if (!addStep(st)) { err = "too many instructions"; return false; }
  }

  if (s_steps.empty()) {
    err = "script is empty";
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Runner
// ---------------------------------------------------------------------------

static void finish(State st, const char *why = nullptr) {
  hid::release();
  s_state = st;
  if (why) s_error = why;
  s_step = 0;
  s_pos = 0;
}

void begin() {
  s_prefs.begin(NVS_NAMESPACE, false);
  s_text = s_prefs.isKey("script") ? s_prefs.getString("script") : String();
  if (s_text.isEmpty()) s_text = presetHello();
}

const String &text() { return s_text; }

bool setText(const String &script) {
  if (script.length() > MAX_PAYLOAD_LEN) return false;
  s_text = script;
  s_prefs.putString("script", s_text);
  return true;
}

bool start(uint32_t countdownMs) {
  if (active()) return false;
  if (!emul::connected()) {
    s_error = "no Bluetooth host connected";
    s_state = ERROR;
    return false;
  }

  s_error = "";
  if (!parse(s_text, s_error)) {
    s_state = ERROR;
    return false;
  }

  s_step = 0;
  s_pos = 0;
  s_nextAt = millis();

  if (countdownMs == 0) countdownMs = COUNTDOWN_MS;
  s_countdownAt = millis() + countdownMs;
  s_state = COUNTDOWN;

  Serial.printf("[hid] payload queued (%u steps), starting in %lu ms\n",
                (unsigned)s_steps.size(), (unsigned long)countdownMs);
  return true;
}

void stop() {
  if (s_state == IDLE || s_state == DONE) return;
  finish(ABORTED);
  Serial.println("[hid] payload aborted, keys released");
}

bool active() { return s_state == COUNTDOWN || s_state == RUNNING; }

void loop() {
  if (s_state == COUNTDOWN) {
    if ((int32_t)(millis() - s_countdownAt) >= 0) {
      s_state = RUNNING;
      s_nextAt = millis();
    }
    return;
  }
  if (s_state != RUNNING) return;

  if (!emul::connected()) {
    finish(ABORTED, "host disconnected");
    return;
  }

  const uint32_t now = millis();
  if ((int32_t)(now - s_nextAt) < 0) return;

  if (s_step >= s_steps.size()) {
    finish(DONE);
    Serial.println("[hid] payload finished");
    return;
  }

  const Step &st = s_steps[s_step];

  if (st.kind == STEP_STRING) {
    if (s_pos >= st.text.length()) {
      s_step++;
      s_pos = 0;
      return;
    }
    hid::typeChar(st.text[s_pos++]);
    s_nextAt = now + s_charDelay;
    return;
  }

  if (st.kind == STEP_DELAY) {
    s_step++;
    s_nextAt = now + st.ms;
    return;
  }

  hid::tap(st.mods, st.usage);
  s_step++;
  s_nextAt = now + 30;
}

const char *state() {
  switch (s_state) {
    case COUNTDOWN: return "countdown";
    case RUNNING:   return "running";
    case DONE:      return "done";
    case ABORTED:   return "aborted";
    case ERROR:     return "error";
    default:        return "idle";
  }
}

uint8_t progress() {
  if (s_steps.empty()) return 0;
  if (s_state == DONE) return 100;
  return (uint8_t)((s_step * 100) / s_steps.size());
}

uint32_t countdownLeft() {
  if (s_state != COUNTDOWN) return 0;
  const int32_t left = (int32_t)(s_countdownAt - millis());
  return left > 0 ? (uint32_t)((left + 999) / 1000) : 0;
}

int steps() { return (int)s_steps.size(); }
int currentStep() { return (int)s_step; }
String error() { return s_error; }

// ---------------------------------------------------------------------------
// Presets
// ---------------------------------------------------------------------------

String presetHello() {
  return "REM Notepad + Hello\n"
         "DELAY 500\n"
         "GUI r\n"
         "DELAY 900\n"
         "STRING notepad\n"
         "ENTER\n"
         "DELAY 1200\n"
         "STRING Hello from Mayavi\n";
}

String presetPowerShell(const String &command) {
  return "REM Custom PowerShell\n"
         "DELAY 500\n"
         "GUI r\n"
         "DELAY 900\n"
         "STRING powershell\n"
         "ENTER\n"
         "DELAY 1500\n"
         "STRING " + command + "\n"
         "ENTER\n";
}

}  // namespace payload
