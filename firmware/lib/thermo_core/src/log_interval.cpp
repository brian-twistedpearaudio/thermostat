#include "log_interval.h"

namespace thermo {

void LogInterval::reset() {
  coveredMs_ = heatMs_ = goodMs_ = humidityMs_ = 0;
  tempSum_ = humiditySum_ = pressureSum_ = 0;
  fault_ = false;
}

bool LogInterval::add(uint32_t nowUtc, uint32_t dtMs, const LogSample& s, LogRecord& out) {
  const uint32_t start = nowUtc - nowUtc % kLogIntervalS;
  bool done = false;

  if (!started_ || start != start_) {
    if (started_ && coveredMs_ > 0) {
      out = LogRecord();
      out.time = start_;
      if (goodMs_ > 0) {
        out.hasTemp = true;
        out.tempF = static_cast<float>(tempSum_ / goodMs_);
        out.pressureHpa = static_cast<float>(pressureSum_ / goodMs_);
      }
      if (humidityMs_ > 0) {
        out.hasHumidity = true;
        out.humidity = static_cast<float>(humiditySum_ / humidityMs_);
      }
      out.setpointF = last_.setpointF;
      out.heatPct = static_cast<uint8_t>((static_cast<uint64_t>(heatMs_) * 100 + coveredMs_ / 2) / coveredMs_);
      out.sensorFault = fault_;
      out.modeOff = last_.modeOff;
      out.partial = coveredMs_ < (kLogIntervalS - 10) * 1000;
      done = true;
    }
    started_ = true;
    start_ = start;
    reset();
  }

  if (dtMs > 10000) dtMs = 10000;  // a stall must not outweigh the rest
  coveredMs_ += dtMs;
  if (s.heating) heatMs_ += dtMs;
  if (s.ok) {
    goodMs_ += dtMs;
    tempSum_ += static_cast<double>(s.tempF) * dtMs;
    pressureSum_ += static_cast<double>(s.pressureHpa) * dtMs;
    if (s.hasHumidity) {
      humidityMs_ += dtMs;
      humiditySum_ += static_cast<double>(s.humidity) * dtMs;
    }
  } else {
    fault_ = true;
  }
  last_ = s;
  return done;
}

}  // namespace thermo
