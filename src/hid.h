// BLE HID reports: keyboard (1), mouse (2), gamepad (3), consumer (4).
#pragma once

#include <Arduino.h>

class BLEHIDDevice;

namespace hid {

// Report descriptor shared by every profile, so switching emulated device
// class never needs a service restart.
extern const uint8_t kReportMap[];
extern const uint16_t kReportMapLen;

void attach(BLEHIDDevice *dev);
void setConnected(bool connected);

// ASCII -> HID usage/keycode, US layout. Returns false for untypable bytes.
bool charUsage(char c, uint8_t &mods, uint16_t &usage);

// Named key ("ENTER", "F5", "RIGHT", ...) -> usage. Modifier-only names return
// usage 0 and set mods.
bool namedKey(const String &name, uint8_t &mods, uint16_t &usage);

void typeChar(char c);
void tap(uint8_t mods, uint16_t usage);
void release();

void mouseMove(int8_t dx, int8_t dy);
void media(uint16_t usage);

}  // namespace hid
