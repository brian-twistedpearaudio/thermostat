#include "datalog.h"

#include <Arduino.h>
#include <time.h>

#include "uplink.h"

namespace datalog {

static thermo::LogInterval s_interval;
static uint32_t s_lastMs = 0;

static void print(const thermo::LogRecord& r) {
  const time_t t = r.time;
  struct tm tm;
  localtime_r(&t, &tm);
  char when[24], temp[12] = "-", hum[12] = "-";
  strftime(when, sizeof(when), "%Y-%m-%d %H:%M", &tm);
  if (r.hasTemp) snprintf(temp, sizeof(temp), "%.2f", r.tempF);
  if (r.hasHumidity) snprintf(hum, sizeof(hum), "%.0f", r.humidity);
  Serial.printf("Record %s: temp %s, humidity %s, set %.1f, heat %u%%%s%s%s\n", when, temp, hum, r.setpointF,
                r.heatPct, r.sensorFault ? ", sensor fault" : "", r.modeOff ? ", mode off" : "",
                r.partial ? ", partial" : "");
}

void tick(bool timeValid, uint32_t nowUtc, const thermo::LogSample& s) {
  const uint32_t ms = millis();
  const uint32_t dt = ms - s_lastMs;
  s_lastMs = ms;
  if (!timeValid) return;

  thermo::LogRecord rec;
  if (!s_interval.add(nowUtc, dt, s, rec)) return;
  print(rec);
  uplink::add(rec);
}

}  // namespace datalog
