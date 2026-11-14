// Time-of-day set point schedule (same every day) and manual override handling.
// No hardware calls: testable on the PC.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace thermo {

struct ScheduleEntry {
  uint16_t minuteOfDay;  // 0..1439, local time
  float setpointF;
};

class Schedule {
 public:
  static constexpr size_t kMaxEntries = 6;

  // Replaces the schedule. Entries may be in any order; they are stored sorted.
  // Returns false (and changes nothing) if there are too many entries, a time is
  // out of range or duplicated, or a set point is outside the allowed range.
  bool set(const ScheduleEntry* entries, size_t n);
  void clear() { count_ = 0; }

  size_t count() const { return count_; }
  const ScheduleEntry& entry(size_t i) const { return entries_[i]; }

  // Index of the entry in force at minuteOfDay. Before the first entry of the
  // day, the last entry (carried over from yesterday) applies. -1 if empty.
  int activeIndex(uint16_t minuteOfDay) const;

 private:
  ScheduleEntry entries_[kMaxEntries] = {};
  size_t count_ = 0;
};

// Decides the set point in force: the schedule's, unless the user changed it by
// hand, in which case the manual value lasts until the next schedule event.
class SetpointManager {
 public:
  explicit SetpointManager(float defaultF = 68.0f);

  void manualSet(float f);

  // Call regularly (at least once a minute). With no valid time or an empty
  // schedule the current set point is kept. Returns the set point in force.
  float update(const Schedule& s, bool timeValid, uint16_t minuteOfDay);

  float setpoint() const { return setpointF_; }
  bool overridden() const { return overridden_; }

 private:
  float setpointF_;
  bool overridden_ = false;
  bool haveLast_ = false;
  uint16_t lastMinute_ = 0;
};

}  // namespace thermo
