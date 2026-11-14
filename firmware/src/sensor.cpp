#include "sensor.h"

#include <Adafruit_BME680.h>
#include <Arduino.h>
#include <Wire.h>

#include "config.h"

namespace sensor {

static Adafruit_BME680 s_bme(&Wire);
static Reading s_reading;
static bool s_present = false;
static bool s_pending = false;
static bool s_haveSmoothed = false;
static uint32_t s_endTimeMs = 0;
static uint32_t s_lastStartMs = 0;
static uint8_t s_failures = 0;

static const uint8_t kMaxFailures = 3;

static void configure() {
  s_bme.setTemperatureOversampling(BME680_OS_8X);
  s_bme.setHumidityOversampling(BME680_OS_2X);
  s_bme.setPressureOversampling(BME680_OS_4X);
  s_bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
  s_bme.setGasHeater(0, 0);  // gas sensor off: no heater self-heating
}

bool begin() {
  s_present = s_bme.begin(0x77) || s_bme.begin(0x76);
  if (s_present) configure();
  s_lastStartMs = millis() - SENSOR_PERIOD_MS;  // read straight away
  return s_present;
}

static void fail() {
  if (s_failures < kMaxFailures) ++s_failures;
  if (s_failures >= kMaxFailures) {
    s_reading.ok = false;
    s_haveSmoothed = false;
  }
}

void poll() {
  const uint32_t now = millis();

  if (!s_pending) {
    if (now - s_lastStartMs < SENSOR_PERIOD_MS) return;
    s_lastStartMs = now;
    if (!s_present) {
      // Keep trying to find the sensor so a loose connection can recover.
      s_present = s_bme.begin(0x77) || s_bme.begin(0x76);
      if (s_present) configure();
      else { fail(); return; }
    }
    s_endTimeMs = s_bme.beginReading();
    if (s_endTimeMs == 0) { fail(); return; }
    s_pending = true;
    return;
  }

  if (static_cast<int32_t>(now - s_endTimeMs) < 0) return;  // conversion still running
  s_pending = false;
  if (!s_bme.endReading()) { fail(); return; }

  const float tempF = s_bme.temperature * 9.0f / 5.0f + 32.0f + TEMP_OFFSET_F;
  if (isnan(tempF) || tempF < -20.0f || tempF > 130.0f) { fail(); return; }

  s_failures = 0;
  if (s_haveSmoothed) {
    s_reading.tempF += TEMP_SMOOTHING * (tempF - s_reading.tempF);
  } else {
    s_reading.tempF = tempF;
    s_haveSmoothed = true;
  }
  s_reading.humidity = s_bme.humidity;
  s_reading.pressureHpa = s_bme.pressure / 100.0f;
  s_reading.ok = true;
}

const Reading& latest() { return s_reading; }

}  // namespace sensor
