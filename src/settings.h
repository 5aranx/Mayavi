// Persisted configuration (NVS): BLE identity, advertising mode, AP credentials.
#pragma once

#include <Arduino.h>

// Advertising flavour.
enum AdvMode : uint8_t {
  ADV_SWIFT_PAIR = 0,  // Microsoft Swift Pair manufacturer beacon
  ADV_BEACON     = 1,  // plain connectable advertisement, no vendor tags
};

struct Settings {
  char     bleName[32];     // advertised name
  uint16_t appearance;      // Bluetooth SIG appearance code
  uint8_t  mode;            // AdvMode
  uint8_t  profile;         // index into the profile table
  uint16_t discoverySec;    // how long discovery advertising runs
  char     apSsid[33];      // dashboard access point
  char     apPass[64];
};

namespace settings {

void begin();
Settings &get();
void save();

// Builds apSsid from the factory MAC: "Mayavi-XXXX".
void resetApName();

}  // namespace settings
