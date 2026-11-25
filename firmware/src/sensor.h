// Temperature sensor on the shared I2C bus. Detects which Bosch part is fitted
// by its chip ID, at address 0x76 or 0x77:
//   BMP280 (0x56-0x58)  temperature + pressure, no humidity
//   BME280 (0x60)       temperature + pressure + humidity
//   BME688 (0x61)       temperature + pressure + humidity (gas heater left off)
#pragma once

namespace sensor {

struct Reading {
  bool ok = false;           // false until the first good reading, and on a fault
  float tempF = 0;           // smoothed, calibration offset applied
  bool hasHumidity = false;  // false for a BMP280
  float humidity = 0;        // % RH, valid only when hasHumidity
  float pressureHpa = 0;
};

bool begin();            // Wire must already be started; true if a sensor was found
void poll();             // call often from loop(); never blocks for long
const Reading& latest();
void setOffset(float offsetF);  // takes effect on the current reading straight away
const char* model();     // "BMP280", "BME280", "BME688" or "none"

}  // namespace sensor
