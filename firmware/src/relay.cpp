#include "relay.h"

#include <Arduino.h>

#include "pins.h"

namespace relay {

static bool s_on = false;

void begin() {
  digitalWrite(PIN_RELAY, LOW);
  pinMode(PIN_RELAY, OUTPUT);
  digitalWrite(PIN_RELAY, LOW);
  s_on = false;
}

void set(bool on) {
  if (on == s_on) return;
  s_on = on;
  digitalWrite(PIN_RELAY, on ? HIGH : LOW);
  Serial.printf("[%lu s] Relay %s\n", millis() / 1000UL, on ? "ON" : "off");
}

bool isOn() { return s_on; }

}  // namespace relay
