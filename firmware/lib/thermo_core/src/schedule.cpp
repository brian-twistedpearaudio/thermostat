#include "schedule.h"

#include "control.h"

namespace thermo {

bool Schedule::set(const ScheduleEntry* entries, size_t n) {
  if (n > kMaxEntries) return false;
  for (size_t i = 0; i < n; ++i) {
    if (entries[i].minuteOfDay >= 1440) return false;
    if (entries[i].setpointF < kMinSetpointF || entries[i].setpointF > kMaxSetpointF) return false;
    for (size_t j = 0; j < i; ++j) {
      if (entries[j].minuteOfDay == entries[i].minuteOfDay) return false;
    }
  }
  // Insertion sort by time into our own storage.
  for (size_t i = 0; i < n; ++i) {
    size_t pos = i;
    while (pos > 0 && entries_[pos - 1].minuteOfDay > entries[i].minuteOfDay) {
      entries_[pos] = entries_[pos - 1];
      --pos;
    }
    entries_[pos] = entries[i];
  }
  count_ = n;
  return true;
}

int Schedule::activeIndex(uint16_t minuteOfDay) const {
  if (count_ == 0) return -1;
  int idx = static_cast<int>(count_) - 1;  // carried over from yesterday
  for (size_t i = 0; i < count_; ++i) {
    if (entries_[i].minuteOfDay <= minuteOfDay) idx = static_cast<int>(i);
  }
  return idx;
}

SetpointManager::SetpointManager(float defaultF) : setpointF_(clampSetpoint(defaultF)) {}

void SetpointManager::manualSet(float f) {
  setpointF_ = clampSetpoint(f);
  overridden_ = true;
}

// True if time t was reached going from minute a to minute b (wrapping at midnight).
static bool crossed(uint16_t a, uint16_t b, uint16_t t) {
  if (a < b) return t > a && t <= b;
  if (a > b) return t > a || t <= b;
  return false;
}

float SetpointManager::update(const Schedule& s, bool timeValid, uint16_t minuteOfDay) {
  if (!timeValid || s.count() == 0) {
    haveLast_ = false;
    return setpointF_;
  }
  if (haveLast_ && overridden_) {
    for (size_t i = 0; i < s.count(); ++i) {
      if (crossed(lastMinute_, minuteOfDay, s.entry(i).minuteOfDay)) {
        overridden_ = false;
        break;
      }
    }
  }
  lastMinute_ = minuteOfDay;
  haveLast_ = true;
  if (!overridden_) {
    setpointF_ = s.entry(static_cast<size_t>(s.activeIndex(minuteOfDay))).setpointF;
  }
  return setpointF_;
}

}  // namespace thermo
