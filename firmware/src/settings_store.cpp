#include "settings_store.h"

#include <Arduino.h>
#include <Preferences.h>

namespace settings_store {

static const char* kNamespace = "thermostat";
static const uint32_t kQuietMs = 2000;   // a run of presses becomes one flash write
static const uint32_t kRetryMs = 30000;  // after a failed write

static thermo::Settings s_stored;
static thermo::Settings s_pending;
static bool s_dirty = false;
static uint32_t s_dueMs = 0;

thermo::Settings load() {
  thermo::Settings s;
  Preferences p;
  if (p.begin(kNamespace, true)) {  // fails on first boot: defaults are used
    s.heatMode = p.getBool("heat", s.heatMode);
    s.setpointF = p.getInt("setpoint", s.setpointF);
    s.swingTenths = p.getInt("swing", s.swingTenths);
    s.offsetTenths = p.getInt("offset", s.offsetTenths);
    s.brightnessPct = p.getInt("bright", s.brightnessPct);
    s.dimSeconds = p.getInt("dim", s.dimSeconds);
    p.end();
  }
  thermo::sanitize(s);
  s_stored = s;
  return s;
}

void changed(const thermo::Settings& s) {
  s_pending = s;
  s_dirty = true;
  s_dueMs = millis() + kQuietMs;
}

static bool putInt(Preferences& p, const char* key, int value, int& stored) {
  if (value == stored) return true;
  if (p.putInt(key, value) == 0) return false;
  stored = value;
  return true;
}

static bool save() {
  Preferences p;
  if (!p.begin(kNamespace, false)) return false;
  bool ok = true;
  if (s_pending.heatMode != s_stored.heatMode) {
    if (p.putBool("heat", s_pending.heatMode)) s_stored.heatMode = s_pending.heatMode;
    else ok = false;
  }
  ok &= putInt(p, "setpoint", s_pending.setpointF, s_stored.setpointF);
  ok &= putInt(p, "swing", s_pending.swingTenths, s_stored.swingTenths);
  ok &= putInt(p, "offset", s_pending.offsetTenths, s_stored.offsetTenths);
  ok &= putInt(p, "bright", s_pending.brightnessPct, s_stored.brightnessPct);
  ok &= putInt(p, "dim", s_pending.dimSeconds, s_stored.dimSeconds);
  p.end();
  return ok;
}

void poll() {
  if (!s_dirty || static_cast<int32_t>(millis() - s_dueMs) < 0) return;
  if (save()) {
    s_dirty = false;
    Serial.println("Settings saved");
  } else {
    s_dueMs = millis() + kRetryMs;
    Serial.println("Settings save FAILED, will retry");
  }
}

}  // namespace settings_store
