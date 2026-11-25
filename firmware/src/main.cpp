// Thermostat firmware — sensor, control, relay, screens, Wi-Fi, network time and 5-minute records.
// Not yet included: graph/info screens, REST API.
#include <Arduino.h>
#include <Wire.h>
#include <esp_task_wdt.h>
#include <esp_timer.h>
#include <time.h>

#include "config.h"
#include "control.h"
#include "datalog.h"
#include "display.h"
#include "net.h"
#include "pins.h"
#include "relay.h"
#include "schedule.h"
#include "sensor.h"
#include "settings_store.h"
#include "ui_home.h"
#include "ui_idle.h"
#include "ui_settings.h"
#include "ui_wifi.h"
#include "uplink.h"

static thermo::Controller s_controller;
static thermo::Schedule s_schedule;  // empty until defaults are supplied
static thermo::SetpointManager s_setpoints;
static thermo::Settings s_settings;

static uint32_t s_lastControlMs = 0;

static uint32_t uptimeSeconds() {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000000LL);
}

static bool clockSet() { return time(nullptr) > 1700000000; }

static void logTick() {
  const sensor::Reading& r = sensor::latest();
  thermo::LogSample s;
  s.ok = r.ok;
  s.tempF = r.tempF;
  s.hasHumidity = r.hasHumidity;
  s.humidity = r.humidity;
  s.pressureHpa = r.pressureHpa;
  s.setpointF = s_controller.setpoint();
  s.heating = s_controller.heating();
  s.modeOff = !s_settings.heatMode;
  datalog::tick(clockSet(), static_cast<uint32_t>(time(nullptr)), s);
}

// Runs the schedule, control logic and relay, then refreshes the screen.
static void controlAndRefresh() {
  // Local time is only valid once the clock has been set by network time.
  struct tm tmNow = {};
  const time_t now = time(nullptr);
  const bool timeValid = clockSet();
  if (timeValid) localtime_r(&now, &tmNow);
  const uint16_t minuteOfDay = static_cast<uint16_t>(tmNow.tm_hour * 60 + tmNow.tm_min);

  const sensor::Reading& r = sensor::latest();

  s_controller.setSetpoint(s_setpoints.update(s_schedule, timeValid, minuteOfDay));
  const bool heat = s_controller.update(r.tempF, r.ok, uptimeSeconds());
  relay::set(heat);

  ui::HomeState st;
  st.sensorOk = r.ok;
  st.tempF = r.tempF;
  st.hasHumidity = r.hasHumidity;
  st.humidity = r.humidity;
  st.setpointF = s_controller.setpoint();
  st.heating = heat;
  st.modeOff = !s_settings.heatMode;
  st.overridden = s_setpoints.overridden() && s_schedule.count() > 0;
  st.timeValid = timeValid;
  st.hour = static_cast<uint8_t>(tmNow.tm_hour);
  st.minute = static_cast<uint8_t>(tmNow.tm_min);
#if UI_DEMO_CYCLE
  // Display-only demo (see config.h): the relay above is unaffected.
  st.sensorOk = true;
  st.tempF = NO_SENSOR_DISPLAY_TEMP_F;
  st.hasHumidity = true;
  st.humidity = 41;
  st.heating = (millis() / UI_DEMO_PERIOD_MS) % 2 == 0;
#endif
  ui::homeUpdate(st);
  ui::idleUpdate(st);
}

static void onSetpointDelta(int delta) {
  s_setpoints.manualSet(s_setpoints.setpoint() + delta * SETPOINT_STEP_F);
  const int f = static_cast<int>(lroundf(s_setpoints.setpoint()));
  if (f != s_settings.setpointF) {
    s_settings.setpointF = f;
    settings_store::changed(s_settings);
  }
  controlAndRefresh();
}

// Pushes the settings to everything that uses them.
static void applySettings() {
  thermo::ControlConfig cfg;
  cfg.swingF = s_settings.swingTenths / 10.0f;
  cfg.minOnS = MIN_ON_SECONDS;
  cfg.minOffS = MIN_OFF_SECONDS;
  s_controller.setConfig(cfg);
  s_controller.setMode(s_settings.heatMode ? thermo::Mode::Heat : thermo::Mode::Off);
  sensor::setOffset(s_settings.offsetTenths / 10.0f);
  display::setBrightness(s_settings.brightnessPct);
  display::setDimTimeout(s_settings.dimSeconds);
  ui::settingsUpdate(s_settings);
}

static void onSettingStep(thermo::SettingId id, int dir) {
  if (!thermo::stepSetting(s_settings, id, dir)) return;
  settings_store::changed(s_settings);
  applySettings();
  controlAndRefresh();  // Mode Off must release the relay at once
}

static void refreshWifi() {
  const net::Status w = net::status();
  ui::homeSetName(w.name);
  char serverText[48];
  uplink::summary(serverText, sizeof(serverText));
  ui::wifiUpdate(w, serverText);
  ui::settingsSetWifi(ui::wifiSummary(w));
  display::keepAwake(w.state == net::State::Starting || w.state == net::State::Hotspot ||
                     w.state == net::State::Trying);

  static bool timeReported = false;
  if (!timeReported && clockSet()) {
    timeReported = true;
    Serial.println("Clock is set");
  }
}

void setup() {
  relay::begin();  // first: make sure the boiler call is off

  Serial.begin(115200);
  setenv("TZ", TIMEZONE, 1);
  tzset();

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

  s_settings = settings_store::load();
  s_setpoints = thermo::SetpointManager(static_cast<float>(s_settings.setpointF));

  display::begin();
  ui::homeCreate(onSetpointDelta, ui::settingsShow);
  ui::settingsCreate(onSettingStep, ui::homeShow, ui::wifiShow);
  ui::wifiCreate(ui::settingsShow);
  ui::idleCreate();
  display::onDim(ui::idleShow);
  display::onWake(ui::homeShow);
  applySettings();

  if (!sensor::begin()) Serial.println("No BMP280 / BME280 / BME688 found at 0x76 or 0x77");

  controlAndRefresh();
  net::begin();
  uplink::begin();
  refreshWifi();

  // If loop() stops running, reset the chip; relay::begin() then starts with heat off.
  if (esp_task_wdt_init(WATCHDOG_SECONDS, true) != ESP_OK || esp_task_wdt_add(nullptr) != ESP_OK) {
    Serial.println("Watchdog setup FAILED");
  }
}

void loop() {
  esp_task_wdt_reset();
  display::loop();
  sensor::poll();
  settings_store::poll();

  const uint32_t now = millis();
  if (now - s_lastControlMs >= 1000) {
    s_lastControlMs = now;
    controlAndRefresh();
    logTick();
    refreshWifi();
  }
  delay(5);
}
