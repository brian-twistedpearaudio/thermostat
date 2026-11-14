// Thermostat firmware — stage 1: sensor, control, relay, home screen.
// Not yet included: logging, graph/settings screens, Wi-Fi, REST API, stored settings.
#include <Arduino.h>
#include <Wire.h>
#include <esp_timer.h>
#include <time.h>

#include "config.h"
#include "control.h"
#include "display.h"
#include "pins.h"
#include "relay.h"
#include "schedule.h"
#include "sensor.h"
#include "ui_home.h"

static thermo::Controller s_controller;
static thermo::Schedule s_schedule;  // empty until defaults are supplied
static thermo::SetpointManager s_setpoints(DEFAULT_SETPOINT_F);

static uint32_t s_lastControlMs = 0;

static uint32_t uptimeSeconds() {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000000LL);
}

// Runs the schedule, control logic and relay, then refreshes the screen.
static void controlAndRefresh() {
  // Local time is only valid once the clock has been set (NTP arrives with Wi-Fi).
  struct tm tmNow = {};
  const time_t now = time(nullptr);
  const bool timeValid = now > 1700000000;
  if (timeValid) localtime_r(&now, &tmNow);
  const uint16_t minuteOfDay = static_cast<uint16_t>(tmNow.tm_hour * 60 + tmNow.tm_min);

  const sensor::Reading& r = sensor::latest();

  s_controller.setSetpoint(s_setpoints.update(s_schedule, timeValid, minuteOfDay));
  const bool heat = s_controller.update(r.tempF, r.ok, uptimeSeconds());
  relay::set(heat);

  ui::HomeState st;
  st.sensorOk = r.ok;
  st.tempF = r.tempF;
  st.humidity = r.humidity;
  st.setpointF = s_controller.setpoint();
  st.heating = heat;
  st.overridden = s_setpoints.overridden() && s_schedule.count() > 0;
  st.timeValid = timeValid;
  st.hour = static_cast<uint8_t>(tmNow.tm_hour);
  st.minute = static_cast<uint8_t>(tmNow.tm_min);
  ui::homeUpdate(st);
}

static void onSetpointDelta(int delta) {
  s_setpoints.manualSet(s_setpoints.setpoint() + delta * SETPOINT_STEP_F);
  controlAndRefresh();
}

void setup() {
  relay::begin();  // first: make sure the boiler call is off

  Serial.begin(115200);
  setenv("TZ", TIMEZONE, 1);
  tzset();

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

  thermo::ControlConfig cfg;
  cfg.swingF = DEFAULT_SWING_F;
  cfg.minOnS = MIN_ON_SECONDS;
  cfg.minOffS = MIN_OFF_SECONDS;
  s_controller.setConfig(cfg);

  display::begin();
  ui::homeCreate(onSetpointDelta);

  if (!sensor::begin()) Serial.println("BME688 not found");

  controlAndRefresh();
}

void loop() {
  display::loop();
  sensor::poll();

  const uint32_t now = millis();
  if (now - s_lastControlMs >= 1000) {
    s_lastControlMs = now;
    controlAndRefresh();
  }
  delay(5);
}
