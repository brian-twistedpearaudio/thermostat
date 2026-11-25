#include "sensor.h"

#include <Adafruit_BME280.h>
#include <Adafruit_BME680.h>
#include <Adafruit_BMP280.h>
#include <Arduino.h>
#include <Wire.h>

#include "config.h"

namespace sensor {

enum class Kind : uint8_t { None, BMP280, BME280, BME68x };

static Adafruit_BMP280 s_bmp280(&Wire);
static Adafruit_BME280 s_bme280;
static Adafruit_BME680 s_bme68x(&Wire);

static Kind s_kind = Kind::None;
static uint8_t s_addr = 0;
static uint8_t s_chipId = 0;

static Reading s_reading;
static bool s_pending = false;  // BME688 conversion in progress
static float s_rawF = 0;        // smoothed, before the offset
static float s_offsetF = 0;
static bool s_haveSmoothed = false;
static uint32_t s_endTimeMs = 0;
static uint32_t s_lastStartMs = 0;
static uint8_t s_failures = 0;

static const uint8_t kMaxFailures = 3;
static const uint8_t kRegChipId = 0xD0;       // same register on all three parts
static const uint8_t kRegCtrlMeas280 = 0xF4;  // BMP280 / BME280
static const uint8_t kRegCtrlMeas68x = 0x74;  // BME688

// 0 if the sensor does not answer.
static uint8_t readReg(uint8_t addr, uint8_t reg) {
  Wire.beginTransmission(addr);
  if (Wire.endTransmission() != 0) return 0;  // address check first
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return 0;
  if (Wire.requestFrom(addr, static_cast<uint8_t>(1)) != 1) return 0;
  return Wire.read();
}

static bool detect() {
  s_kind = Kind::None;
  const uint8_t addrs[] = {0x76, 0x77};
  for (uint8_t addr : addrs) {
    const uint8_t id = readReg(addr, kRegChipId);
    if (id == 0x56 || id == 0x57 || id == 0x58) {
      if (s_bmp280.begin(addr, id)) {
        s_bmp280.setSampling(Adafruit_BMP280::MODE_NORMAL, Adafruit_BMP280::SAMPLING_X2,
                             Adafruit_BMP280::SAMPLING_X4, Adafruit_BMP280::FILTER_X4,
                             Adafruit_BMP280::STANDBY_MS_500);
        s_kind = Kind::BMP280;
      }
    } else if (id == 0x60) {
      if (s_bme280.begin(addr, &Wire)) {
        s_bme280.setSampling(Adafruit_BME280::MODE_NORMAL, Adafruit_BME280::SAMPLING_X2,
                             Adafruit_BME280::SAMPLING_X4, Adafruit_BME280::SAMPLING_X2,
                             Adafruit_BME280::FILTER_X4, Adafruit_BME280::STANDBY_MS_500);
        s_kind = Kind::BME280;
      }
    } else if (id == 0x61) {
      if (s_bme68x.begin(addr)) {
        s_bme68x.setTemperatureOversampling(BME680_OS_8X);
        s_bme68x.setHumidityOversampling(BME680_OS_2X);
        s_bme68x.setPressureOversampling(BME680_OS_4X);
        s_bme68x.setIIRFilterSize(BME680_FILTER_SIZE_3);
        s_bme68x.setGasHeater(0, 0);  // gas sensor off: no heater self-heating
        s_kind = Kind::BME68x;
      }
    }
    if (s_kind != Kind::None) {
      s_addr = addr;
      s_chipId = id;
      Serial.printf("Sensor: %s at 0x%02X\n", model(), addr);
      return true;
    }
  }
  return false;
}

static void fail() {
  s_pending = false;
  if (s_failures < kMaxFailures) ++s_failures;
  if (s_failures >= kMaxFailures) {
    s_reading.ok = false;
    s_haveSmoothed = false;
    s_kind = Kind::None;  // look for the sensor again next time
  }
}

// A loose wire can leave the bus returning plausible-looking garbage, so the sensor must answer
// with its chip ID. After a power blip it still does, but has lost its settings (temperature
// oversampling, the top three bits, is 0) and would report a fixed, plausible temperature.
static bool healthy() {
  if (readReg(s_addr, kRegChipId) != s_chipId) return false;
  const uint8_t ctrl = readReg(s_addr, s_kind == Kind::BME68x ? kRegCtrlMeas68x : kRegCtrlMeas280);
  if ((ctrl >> 5) != 0) return true;
  s_kind = Kind::None;  // set it up again next time
  return false;
}

static void accept(float celsius, float humidity, float pressurePa) {
  const float tempF = celsius * 9.0f / 5.0f + 32.0f;
  if (isnan(tempF) || tempF < -20.0f || tempF > 130.0f) {
    fail();
    return;
  }
  s_failures = 0;
  if (s_haveSmoothed) {
    s_rawF += TEMP_SMOOTHING * (tempF - s_rawF);
  } else {
    s_rawF = tempF;
    s_haveSmoothed = true;
  }
  s_reading.tempF = s_rawF + s_offsetF;
  s_reading.hasHumidity = !isnan(humidity);
  s_reading.humidity = s_reading.hasHumidity ? humidity : 0.0f;
  s_reading.pressureHpa = pressurePa / 100.0f;
  s_reading.ok = true;
}

bool begin() {
  s_lastStartMs = millis() - SENSOR_PERIOD_MS;  // read straight away
  return detect();
}

void poll() {
  const uint32_t now = millis();

  if (s_pending) {
    if (static_cast<int32_t>(now - s_endTimeMs) < 0) return;  // still converting
    s_pending = false;
    if (!s_bme68x.endReading()) {
      fail();
      return;
    }
    accept(s_bme68x.temperature, s_bme68x.humidity, s_bme68x.pressure);
    return;
  }

  if (now - s_lastStartMs < SENSOR_PERIOD_MS) return;
  s_lastStartMs = now;

  if (s_kind == Kind::None && !detect()) {
    fail();
    return;
  }
  if (!healthy()) {
    fail();
    return;
  }

  if (s_kind == Kind::BME68x) {
    s_endTimeMs = s_bme68x.beginReading();
    if (s_endTimeMs == 0) fail();
    else s_pending = true;
    return;
  }

  float celsius, humidity = NAN, pressurePa;
  if (s_kind == Kind::BME280) {
    celsius = s_bme280.readTemperature();
    humidity = s_bme280.readHumidity();
    pressurePa = s_bme280.readPressure();
  } else {
    celsius = s_bmp280.readTemperature();
    pressurePa = s_bmp280.readPressure();
  }
  // These libraries do not report a failed read, so check the sensor is still there.
  if (healthy()) accept(celsius, humidity, pressurePa);
  else fail();
}

void setOffset(float offsetF) {
  s_offsetF = offsetF;
  s_reading.tempF = s_rawF + s_offsetF;
}

const Reading& latest() { return s_reading; }

const char* model() {
  switch (s_kind) {
    case Kind::BMP280: return "BMP280";
    case Kind::BME280: return "BME280";
    case Kind::BME68x: return "BME688";
    default: return "none";
  }
}

}  // namespace sensor
