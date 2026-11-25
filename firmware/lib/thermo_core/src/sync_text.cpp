#include "sync_text.h"

#include <math.h>
#include <stdlib.h>

#include "control.h"
#include "wifi_text.h"

namespace thermo {

static const char* const kKeys[kSyncCount] = {"set", "mode", "swing", "offset", "schedule", "name"};

// A whole number, written the one usual way: no plus sign, spaces or leading zeros.
static bool number(const std::string& s, int64_t lo, int64_t hi, int64_t& out) {
  if (s.empty() || s.size() > 12) return false;
  const size_t first = s[0] == '-' ? 1 : 0;
  if (first == s.size()) return false;
  for (size_t i = first; i < s.size(); ++i) {
    if (s[i] < '0' || s[i] > '9') return false;
  }
  if (s[first] == '0' && s.size() > 1) return false;
  out = strtoll(s.c_str(), nullptr, 10);
  return out >= lo && out <= hi;
}

static bool setpoint(const std::string& s, int64_t& out) {
  return number(s, static_cast<int64_t>(kMinSetpointF), static_cast<int64_t>(kMaxSetpointF), out);
}

bool parseSchedule(const std::string& text, Schedule& out) {
  ScheduleEntry entries[Schedule::kMaxEntries];
  size_t n = 0;
  if (text != "-") {
    size_t at = 0;
    for (;;) {
      const size_t comma = text.find(',', at);
      const std::string item = text.substr(at, comma == std::string::npos ? comma : comma - at);
      if (n == Schedule::kMaxEntries || item.size() < 6 || item[4] != '=') return false;
      for (int i = 0; i < 4; ++i) {
        if (item[i] < '0' || item[i] > '9') return false;
      }
      const int hour = (item[0] - '0') * 10 + item[1] - '0';
      const int minute = (item[2] - '0') * 10 + item[3] - '0';
      int64_t f;
      if (hour > 23 || minute > 59 || !setpoint(item.substr(5), f)) return false;
      entries[n].minuteOfDay = static_cast<uint16_t>(hour * 60 + minute);
      entries[n].setpointF = static_cast<float>(f);
      if (n > 0 && entries[n].minuteOfDay <= entries[n - 1].minuteOfDay) return false;
      ++n;
      if (comma == std::string::npos) break;
      at = comma + 1;
    }
  }
  return out.set(entries, n);
}

bool validSyncValue(SyncField f, const std::string& v) {
  int64_t n;
  switch (f) {
    case kSyncSet: return setpoint(v, n);
    case kSyncMode: return v == "heat" || v == "off";
    case kSyncSwing: return number(v, 5, 30, n) && n % 5 == 0;
    case kSyncOffset: return number(v, -100, 100, n);
    case kSyncSchedule: {
      Schedule s;
      return parseSchedule(v, s);
    }
    case kSyncName: {
      std::string name = v;
      return cleanName(name) && name == v;
    }
    case kSyncCount: break;
  }
  return false;
}

static std::string tenths(float f) { return std::to_string(lroundf(f * 10.0f)); }

std::string syncRequest(const SyncFields& fields, const SyncState& state) {
  std::string t = "sync 1\n";
  for (int i = 0; i < kSyncCount; ++i) {
    t += std::string(kKeys[i]) + " " + std::to_string(static_cast<long long>(fields.time[i])) + " " +
         fields.value[i] + "\n";
  }
  t += "state " + (state.sensorOk ? tenths(state.tempF) : std::string("-")) + " " + tenths(state.setpointF) +
       (state.heating ? " 1" : " 0") + (state.manual ? " 1" : " 0") + "\nend\n";
  return t;
}

// The next line, without its end. False if there is none.
static bool line(const std::string& text, size_t& at, std::string& out) {
  const size_t end = text.find('\n', at);
  if (end == std::string::npos) return false;
  out = text.substr(at, end - at);
  at = end + 1;
  return true;
}

bool parseSyncReply(const std::string& text, SyncFields& out) {
  SyncFields f;
  size_t at = 0;
  std::string l;
  if (!line(text, at, l) || l != "sync 1") return false;
  for (int i = 0; i < kSyncCount; ++i) {
    const std::string key = std::string(kKeys[i]) + " ";
    if (!line(text, at, l) || l.compare(0, key.size(), key) != 0) return false;
    const size_t space = l.find(' ', key.size());
    if (space == std::string::npos) return false;
    if (!number(l.substr(key.size(), space - key.size()), 0, 10000000000LL, f.time[i])) return false;
    f.value[i] = l.substr(space + 1);
    if (!validSyncValue(static_cast<SyncField>(i), f.value[i])) return false;
  }
  if (!line(text, at, l) || l != "end" || at != text.size()) return false;
  out = f;
  return true;
}

int64_t syncChangeTime(int64_t previous, bool clockSet, int64_t now) {
  if (!clockSet) return kSyncUnknown;
  return now > previous ? now : previous + 1;
}

}  // namespace thermo
