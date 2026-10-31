#include "hid.h"

#include <BLEHIDDevice.h>
#include <BLECharacteristic.h>

#include "config.h"

namespace hid {

// ---------------------------------------------------------------------------
// Report descriptor
//
// One composite descriptor: keyboard, relative mouse, gamepad and a consumer
// (media) control. Having all four permanently present means a profile switch
// is only an advertising change -- no GATT restart, no re-pairing.
// ---------------------------------------------------------------------------
const uint8_t kReportMap[] = {
    // ---------------- Keyboard, report ID 1 ----------------
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x06,        // Usage (Keyboard)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x01,        //   Report ID (1)
    0x05, 0x07,        //   Usage Page (Keyboard)
    0x19, 0xE0,        //   Usage Minimum (Left Control)
    0x29, 0xE7,        //   Usage Maximum (Right GUI)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x08,        //   Report Count (8)
    0x81, 0x02,        //   Input (Data, Variable, Absolute) -- modifiers
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x08,        //   Report Size (8)
    0x81, 0x01,        //   Input (Constant) -- reserved byte
    0x95, 0x06,        //   Report Count (6)
    0x75, 0x08,        //   Report Size (8)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x65,        //   Logical Maximum (101)
    0x05, 0x07,        //   Usage Page (Keyboard)
    0x19, 0x00,        //   Usage Minimum (0)
    0x29, 0x65,        //   Usage Maximum (101)
    0x81, 0x00,        //   Input (Data, Array) -- key array
    0x95, 0x05,        //   Report Count (5) -- LED output
    0x75, 0x01,
    0x05, 0x08,        //   Usage Page (LEDs)
    0x19, 0x01,
    0x29, 0x05,
    0x91, 0x02,        //   Output (Data, Variable, Absolute)
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x03,
    0x91, 0x01,        //   Output (Constant)
    0xC0,              // End Collection

    // ---------------- Mouse, report ID 2 ----------------
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x02,        // Usage (Mouse)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x02,        //   Report ID (2)
    0x09, 0x01,        //   Usage (Pointer)
    0xA1, 0x00,        //   Collection (Physical)
    0x05, 0x09,        //     Usage Page (Button)
    0x19, 0x01,
    0x29, 0x05,
    0x15, 0x00,
    0x25, 0x01,
    0x95, 0x05,
    0x75, 0x01,
    0x81, 0x02,        //     Input (buttons)
    0x95, 0x01,
    0x75, 0x03,
    0x81, 0x01,        //     Input (padding)
    0x05, 0x01,        //     Usage Page (Generic Desktop)
    0x09, 0x30,        //     Usage (X)
    0x09, 0x31,        //     Usage (Y)
    0x09, 0x38,        //     Usage (Wheel)
    0x15, 0x81,        //     Logical Minimum (-127)
    0x25, 0x7F,        //     Logical Maximum (127)
    0x75, 0x08,
    0x95, 0x03,
    0x81, 0x06,        //     Input (Data, Variable, Relative)
    0xC0,
    0xC0,

    // ---------------- Gamepad, report ID 3 ----------------
    0x05, 0x01,
    0x09, 0x05,        // Usage (Game Pad)
    0xA1, 0x01,
    0x85, 0x03,        //   Report ID (3)
    0x09, 0x30,        //   X
    0x09, 0x31,        //   Y
    0x09, 0x32,        //   Z
    0x09, 0x35,        //   Rz
    0x15, 0x00,
    0x26, 0xFF, 0x00,  //   Logical Maximum (255)
    0x75, 0x08,
    0x95, 0x04,
    0x81, 0x02,        //   Input (4 axes)
    0x05, 0x09,        //   Usage Page (Button)
    0x19, 0x01,
    0x29, 0x08,
    0x15, 0x00,
    0x25, 0x01,
    0x75, 0x01,
    0x95, 0x08,
    0x81, 0x02,        //   Input (8 buttons)
    0xC0,

    // ---------------- Consumer control (media), report ID 4 ----------------
    0x05, 0x0C,        // Usage Page (Consumer)
    0x09, 0x01,        // Usage (Consumer Control)
    0xA1, 0x01,
    0x85, 0x04,        //   Report ID (4)
    0x19, 0x00,        //   Usage Minimum (0)
    0x2A, 0x3C, 0x02,  //   Usage Maximum (0x023C)
    0x15, 0x00,
    0x26, 0x3C, 0x02,  //   Logical Maximum (0x023C)
    0x75, 0x10,
    0x95, 0x01,
    0x81, 0x00,        //   Input (16-bit array)
    0xC0,
};
const uint16_t kReportMapLen = sizeof(kReportMap);

static BLECharacteristic *s_kbd = nullptr;
static BLECharacteristic *s_mouse = nullptr;
static BLECharacteristic *s_pad = nullptr;
static BLECharacteristic *s_media = nullptr;
static bool s_connected = false;

void attach(BLEHIDDevice *dev) {
  s_kbd = dev->inputReport(1);
  s_mouse = dev->inputReport(2);
  s_pad = dev->inputReport(3);
  s_media = dev->inputReport(4);
  dev->outputReport(1);  // LED state from the host; values are ignored
}

void setConnected(bool connected) { s_connected = connected; }

// ---------------------------------------------------------------------------
// Key tables
// ---------------------------------------------------------------------------

bool charUsage(char c, uint8_t &mods, uint16_t &usage) {
  mods = 0;
  usage = 0;

  if (c == '\n' || c == '\r') { usage = 0x28; return true; }  // ENTER
  if (c == '\t')              { usage = 0x2B; return true; }  // TAB
  if (c == 0x7F)              { usage = 0x4C; return true; }  // DELETE
  if (c < 0x20 || c > 0x7E)   return false;

  if (c >= 'a' && c <= 'z') { usage = 0x04 + (c - 'a'); return true; }
  if (c >= 'A' && c <= 'Z') { mods = 0x02; usage = 0x04 + (c - 'A'); return true; }
  if (c >= '1' && c <= '9') { usage = 0x1E + (c - '1'); return true; }
  if (c == '0')             { usage = 0x27; return true; }

  // Punctuation + digits, US layout. Index = ASCII - 0x20.
  struct Pair { uint8_t mods, usage; };
  static const Pair kSym[] = {
      {0, 0x2C}, {2, 0x1E}, {2, 0x34}, {2, 0x20}, {2, 0x21}, {2, 0x22},
      {2, 0x23}, {0, 0x34}, {2, 0x26}, {2, 0x27}, {2, 0x25}, {2, 0x2E},
      {0, 0x36}, {0, 0x2D}, {0, 0x37}, {0, 0x38},
      {0, 0x27}, {0, 0x1E}, {0, 0x1F}, {0, 0x20}, {0, 0x21}, {0, 0x22},
      {0, 0x23}, {0, 0x24}, {0, 0x25}, {0, 0x26},
      {2, 0x33}, {0, 0x33}, {2, 0x36}, {0, 0x2E}, {2, 0x37}, {2, 0x38},
      {2, 0x1F},
      {0, 0x2F}, {0, 0x31}, {0, 0x30}, {2, 0x23}, {2, 0x2D}, {0, 0x35},
      {2, 0x2F}, {2, 0x31}, {2, 0x30}, {2, 0x35},
  };
  auto pick = [&](const Pair &p) {
    mods = p.mods ? 0x02 : 0;
    usage = p.usage;
    return true;
  };

  if (c >= 0x20 && c <= 0x40) return pick(kSym[c - 0x20]);
  if (c >= 0x5B && c <= 0x60) return pick(kSym[33 + (c - 0x5B)]);
  if (c >= 0x7B && c <= 0x7E) return pick(kSym[39 + (c - 0x7B)]);
  return false;
}

bool namedKey(const String &name, uint8_t &mods, uint16_t &usage) {
  mods = 0;
  usage = 0;
  String k = name;
  k.toUpperCase();

  if (k == "CTRL" || k == "CONTROL") { mods = 0x01; return true; }
  if (k == "SHIFT")                  { mods = 0x02; return true; }
  if (k == "ALT")                    { mods = 0x04; return true; }
  if (k == "GUI" || k == "WIN" || k == "SUPER" || k == "CMD") { mods = 0x08; return true; }
  if (k == "RCTRL")                  { mods = 0x10; return true; }
  if (k == "RSHIFT")                 { mods = 0x20; return true; }
  if (k == "RALT")                   { mods = 0x40; return true; }
  if (k == "RGUI")                   { mods = 0x80; return true; }

  struct Named { const char *name; uint8_t usage; };
  static const Named kNames[] = {
      {"ENTER", 0x28},   {"RETURN", 0x28}, {"TAB", 0x2B},    {"ESC", 0x29},
      {"ESCAPE", 0x29},  {"BACKSPACE", 0x2A}, {"BKSP", 0x2A},
      {"SPACE", 0x2C},   {"DELETE", 0x4C}, {"DEL", 0x4C},    {"INSERT", 0x49},
      {"INS", 0x49},     {"HOME", 0x4A},   {"END", 0x4D},
      {"PAGEUP", 0x4B},  {"PGUP", 0x4B},   {"PAGEDOWN", 0x4E}, {"PGDN", 0x4E},
      {"UP", 0x52},      {"DOWN", 0x51},   {"LEFT", 0x50},   {"RIGHT", 0x4F},
      {"CAPSLOCK", 0x39},{"PRINTSCREEN", 0x46}, {"MENU", 0x65},
      {"F1", 0x3A}, {"F2", 0x3B}, {"F3", 0x3C},  {"F4", 0x3D},
      {"F5", 0x3E}, {"F6", 0x3F}, {"F7", 0x40},  {"F8", 0x41},
      {"F9", 0x42}, {"F10", 0x43}, {"F11", 0x44}, {"F12", 0x45},
  };
  for (const Named &n : kNames) {
    if (k == n.name) { usage = n.usage; return true; }
  }
  return false;
}

// ---------------------------------------------------------------------------
// Reports
// ---------------------------------------------------------------------------

void release() {
  if (!s_connected || !s_kbd) return;
  uint8_t r[8] = {0};
  s_kbd->setValue(r, sizeof(r));
  s_kbd->notify();
}

void tap(uint8_t mods, uint16_t usage) {
  if (!s_connected || !s_kbd) return;
  uint8_t r[8] = {0};
  r[0] = mods;
  if (usage >= 0x04 && usage <= 0xE7) r[2] = (uint8_t)usage;
  s_kbd->setValue(r, sizeof(r));
  s_kbd->notify();
  delay(KEY_PRESS_MS);
  release();
}

void typeChar(char c) {
  uint8_t mods = 0;
  uint16_t usage = 0;
  if (!charUsage(c, mods, usage)) {
    // Non-ASCII bytes are sent as '?' so a stray UTF-8 byte is visible rather
    // than silently swallowing the rest of the line.
    mods = 0x02;
    usage = 0x38;
  }
  tap(mods, usage);
}

void mouseMove(int8_t dx, int8_t dy) {
  if (!s_connected || !s_mouse) return;
  uint8_t r[4] = {0, (uint8_t)dx, (uint8_t)dy, 0};
  s_mouse->setValue(r, sizeof(r));
  s_mouse->notify();
}

void media(uint16_t usage) {
  if (!s_connected || !s_media) return;
  uint8_t r[2] = {(uint8_t)(usage & 0xFF), (uint8_t)(usage >> 8)};
  s_media->setValue(r, sizeof(r));
  s_media->notify();
  delay(KEY_PRESS_MS);
  uint8_t z[2] = {0};
  s_media->setValue(z, sizeof(z));
  s_media->notify();
}

}  // namespace hid
