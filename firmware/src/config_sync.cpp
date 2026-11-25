#include "config_sync.h"

#include <Arduino.h>
#include <Preferences.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "net.h"
#include "uplink.h"

namespace config_sync {

using namespace thermo;

static const char* kNamespace = "sync";
static const uint32_t kQuietMs = 2000;   // as settings_store, so both are written together
static const uint32_t kRetryMs = 30000;  // after a failed write
static const uint32_t kNameRetryMs = 10000;

static SyncFields s_fields;                // values as last seen here, and when they changed
static uint32_t s_changes[kSyncCount] = {};  // counts changes made here, to notice one during a call
static bool s_soon = false;  // a change made here is waiting to be sent
static bool s_dirty = false;
static uint32_t s_dueMs = 0;

// The name is saved by the network task. Until it has been, the old one is not a change.
static bool s_namePending = false;
static std::string s_oldName;
static uint32_t s_nameAskedMs = 0;

static void saveSoon() {
  s_dirty = true;
  s_dueMs = millis() + kQuietMs;
}

static std::string valueOf(SyncField f, const Settings& s) {
  switch (f) {
    case kSyncSet: return std::to_string(s.setpointF);
    case kSyncMode: return s.heatMode ? "heat" : "off";
    case kSyncSwing: return std::to_string(s.swingTenths);
    case kSyncOffset: return std::to_string(s.offsetTenths);
    case kSyncSchedule: return s_fields.value[kSyncSchedule];  // changed only by the server
    case kSyncName: return net::status().name;
    case kSyncCount: break;
  }
  return "";
}

void begin(const Settings& settings, Schedule& schedule) {
  std::string text = "-";
  Preferences p;
  if (p.begin(kNamespace, true)) {  // fails on first boot
    int64_t times[kSyncCount];
    if (p.isKey("times") && p.getBytes("times", times, sizeof(times)) == sizeof(times)) {
      memcpy(s_fields.time, times, sizeof(times));
    }
    if (p.isKey("schedule")) text = p.getString("schedule").c_str();
    p.end();
  }
  if (!parseSchedule(text, schedule)) {
    text = "-";
    s_fields.time[kSyncSchedule] = kSyncNever;  // the server's copy is taken again
  }
  s_fields.value[kSyncSchedule] = text;
  for (int i = 0; i < kSyncCount; ++i) s_fields.value[i] = valueOf(static_cast<SyncField>(i), settings);
}

static bool save() {
  Preferences p;
  if (!p.begin(kNamespace, false)) return false;
  const std::string& text = s_fields.value[kSyncSchedule];
  // The schedule first: with the old time it is taken again if power is lost in between.
  bool ok = p.putString("schedule", text.c_str()) == text.size();
  ok = ok && p.putBytes("times", s_fields.time, sizeof(s_fields.time)) == sizeof(s_fields.time);
  p.end();
  return ok;
}

// A change made on the thermostat, found by its value.
static void noticeChanges(const Settings& settings, bool clockSet) {
  bool changed = false;
  for (int i = 0; i < kSyncCount; ++i) {
    const std::string v = valueOf(static_cast<SyncField>(i), settings);
    if (i == kSyncName && s_namePending) {
      if (v == s_fields.value[i]) {
        s_namePending = false;
      } else if (v == s_oldName) {
        if (millis() - s_nameAskedMs >= kNameRetryMs) {
          s_nameAskedMs = millis();
          net::requestName(s_fields.value[i].c_str());
        }
        continue;
      } else {
        s_namePending = false;  // a third name: entered on the setup page meanwhile
      }
    }
    if (v == s_fields.value[i]) continue;
    s_fields.value[i] = v;
    s_fields.time[i] = syncChangeTime(s_fields.time[i], clockSet, time(nullptr));
    ++s_changes[i];
    changed = true;
  }
  if (changed) {
    saveSoon();
    s_soon = true;
  }
}

void tick(const Settings& settings, const SyncState& state, bool clockSet) {
  noticeChanges(settings, clockSet);

  uplink::SyncRequest r;
  r.text = syncRequest(s_fields, state);
  memcpy(r.changes, s_changes, sizeof(s_changes));
  uplink::offerSync(r, s_soon);
  s_soon = false;

  if (s_dirty && static_cast<int32_t>(millis() - s_dueMs) >= 0) {
    s_dirty = !save();
    if (s_dirty) {
      s_dueMs = millis() + kRetryMs;
      Serial.println("Sync save FAILED, will retry");
    }
  }
}

static void apply(SyncField f, const std::string& v, Settings& s, Schedule& schedule) {
  switch (f) {
    case kSyncSet: s.setpointF = atoi(v.c_str()); break;
    case kSyncMode: s.heatMode = v == "heat"; break;
    case kSyncSwing: s.swingTenths = atoi(v.c_str()); break;
    case kSyncOffset: s.offsetTenths = atoi(v.c_str()); break;
    case kSyncSchedule: parseSchedule(v, schedule); break;
    case kSyncName:
      s_oldName = net::status().name;
      s_namePending = true;
      s_nameAskedMs = millis();
      net::requestName(v.c_str());
      break;
    case kSyncCount: break;
  }
}

bool poll(Settings& settings, Schedule& schedule, bool clockSet, bool& setpoint) {
  uplink::SyncReply reply;
  setpoint = false;
  if (!uplink::takeSync(reply)) return false;
  noticeChanges(settings, clockSet);  // one made since the last tick must not be overwritten

  bool any = false;
  for (int i = 0; i < kSyncCount; ++i) {
    const SyncField f = static_cast<SyncField>(i);
    if (reply.changes[i] != s_changes[i]) continue;  // changed here after the request was made
    const bool sameValue = reply.fields.value[i] == s_fields.value[i];
    const int64_t before = s_fields.time[i];
    if (sameValue && reply.fields.time[i] == before) continue;
    s_fields.time[i] = reply.fields.time[i];
    s_fields.value[i] = reply.fields.value[i];
    saveSoon();
    if (!sameValue) {
      apply(f, s_fields.value[i], settings, schedule);
      any = true;
      Serial.printf("From server: %s\n", i == kSyncName ? "name" : s_fields.value[i].c_str());
    }
    // The same set point set again, unless the new time is only the server's for a change made here.
    if (f == kSyncSet && (!sameValue || (before != kSyncUnknown && reply.fields.time[i] > before))) {
      setpoint = true;
      any = true;
    }
  }
  return any;
}

}  // namespace config_sync
