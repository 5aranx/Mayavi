// BLE emulation: HID device, advertising profiles, pairing state.
#pragma once

#include <Arduino.h>

#include "settings.h"

struct ProfileDef {
  const char *id;
  const char *label;
  uint16_t appearance;
  const char *name;
  const char *hint;  // what the host will see
};

namespace emul {

void begin();
void loop();

// Discovery advertising, bounded by `seconds` (0 = stored default).
bool start(uint32_t seconds = 0);
void stop();
void forgetBonds();

bool advertising();
bool connected();
bool ready();          // encrypted link, reports are delivered
int bonds();
uint32_t secondsLeft();  // 0 when the window is open-ended

int profileCount();
const ProfileDef *profile(int i);
int currentProfile();
bool setProfile(const String &id);
String currentProfileId();

void setName(const char *name);
void setMode(uint8_t mode);
void setAppearance(uint16_t appearance);

const char *stateText();

}  // namespace emul
