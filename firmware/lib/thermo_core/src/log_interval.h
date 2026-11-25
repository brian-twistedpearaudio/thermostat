// Averages readings into one record per 5 minutes, for sending to the server.
// No hardware calls: testable on the PC.
#pragma once
#include <stdint.h>

namespace thermo {

constexpr uint32_t kLogIntervalS = 300;

struct LogRecord {
  uint32_t time = 0;         // UTC start of the interval, a multiple of kLogIntervalS
  bool hasTemp = false;      // false if there was no good reading
  float tempF = 0;           // average
  bool hasHumidity = false;
  float humidity = 0;        // average %
  float pressureHpa = 0;     // average, valid only when hasTemp
  float setpointF = 0;       // at the end of the interval
  uint8_t heatPct = 0;       // share of the time covered that heat was called for
  bool sensorFault = false;  // the sensor was not ok for some of the interval
  bool modeOff = false;      // mode was Off at the end of the interval
  bool partial = false;      // covers less than the whole interval (restart, clock set)
};

struct LogSample {
  bool ok = false;
  float tempF = 0;
  bool hasHumidity = false;
  float humidity = 0;
  float pressureHpa = 0;
  float setpointF = 0;
  bool heating = false;
  bool modeOff = false;
};

class LogInterval {
 public:
  // Call about once a second while the time is known. dtMs is the time since the last call.
  // True when `out` holds the interval that just ended.
  bool add(uint32_t nowUtc, uint32_t dtMs, const LogSample& s, LogRecord& out);

 private:
  void reset();

  bool started_ = false;
  uint32_t start_ = 0;
  uint32_t coveredMs_ = 0, heatMs_ = 0;
  uint32_t goodMs_ = 0, humidityMs_ = 0;
  double tempSum_ = 0, humiditySum_ = 0, pressureSum_ = 0;
  LogSample last_;
  bool fault_ = false;
};

}  // namespace thermo
