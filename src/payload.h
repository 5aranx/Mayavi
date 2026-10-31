// DuckyScript-style payload engine. Non-blocking: the runner is a state
// machine so the dashboard stays responsive and Stop works mid-script.
#pragma once

#include <Arduino.h>

namespace payload {

void begin();  // load the stored script

const String &text();
bool setText(const String &script);

// Requires a connected host. `countdownMs` gives the operator time to look at
// the target screen before the first keystroke.
bool start(uint32_t countdownMs = 0);
void stop();
void loop();

bool active();
const char *state();       // idle | countdown | running | done | aborted | error
uint8_t progress();        // 0..100
uint32_t countdownLeft();  // seconds
int steps();
int currentStep();
String error();

String presetHello();
String presetPowerShell(const String &command);

}  // namespace payload
