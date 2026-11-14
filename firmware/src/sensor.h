// BME688 temperature / humidity / pressure. Gas heater is left off.
#pragma once

namespace sensor {

struct Reading {
  bool ok = false;         // false until the first good reading, and on a fault
  float tempF = 0;         // smoothed, calibration offset applied
  float humidity = 0;      // % RH
  float pressureHpa = 0;
};

bool begin();            // Wire must already be started
void poll();             // call often from loop(); never blocks for long
const Reading& latest();

}  // namespace sensor
