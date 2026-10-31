// Mayavi -- shared configuration.
#pragma once

#define MAYAVI_VERSION "1.0.0"
#define MAYAVI_HOSTNAME "mayavi"

// Access point identity. The full SSID is "Mayavi-XXXX", where XXXX comes
// from the low bytes of the factory MAC (see settings.cpp).
//
// Empty password = open network (no WPA2). Set one from the dashboard or with
// `wifi set <ssid> <pass>` if you want it locked.
#define AP_SSID_PREFIX   "Mayavi"
#define AP_PASSWORD_DFLT ""

// Onboard WS2812 status LED (overridable from platformio.ini).
#ifndef STATUS_LED_PIN
#define STATUS_LED_PIN 48
#endif

// NVS namespace for every persisted setting.
#define NVS_NAMESPACE "mayavi"

// Limits
#define MAX_BLE_NAME        25   // advertised device name, bytes
#define MAX_PAYLOAD_LEN     4096 // DuckyScript buffer
#define MAX_PAYLOAD_STEPS   192  // parsed instructions per script

#define DISCOVERY_MIN_SEC   5
#define DISCOVERY_MAX_SEC   60
#define DISCOVERY_DEF_SEC   30

// Timing (milliseconds)
#define COUNTDOWN_MS        3000 // lead time before a payload starts typing
#define CHAR_DELAY_MS       14   // default gap between typed characters
#define KEY_PRESS_MS        8    // key-down hold time
