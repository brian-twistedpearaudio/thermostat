// Configuration sync with the server: text and checks. The contract is docs/sync.md.
// No hardware calls: testable on the PC.
#pragma once
#include <stdint.h>

#include <string>

#include "schedule.h"

namespace thermo {

// In the order of the lines in the text.
enum SyncField { kSyncSet, kSyncMode, kSyncSwing, kSyncOffset, kSyncSchedule, kSyncName, kSyncCount };

static const int64_t kSyncNever = 0;     // never changed
static const int64_t kSyncUnknown = -1;  // changed while the clock was not set

struct SyncFields {
  std::string value[kSyncCount];
  int64_t time[kSyncCount] = {};  // UTC of the last change, or one of the above
};

struct SyncState {
  bool sensorOk = false;
  float tempF = 0;
  float setpointF = 0;
  bool heating = false;
  bool manual = false;
};

// "HHMM=F,..." in order of time, or "-" for none. False, with `out` unchanged, if it is not valid.
bool parseSchedule(const std::string& text, Schedule& out);

bool validSyncValue(SyncField f, const std::string& value);

std::string syncRequest(const SyncFields& fields, const SyncState& state);

// False if any part of the reply is not as expected; `out` is then unchanged.
bool parseSyncReply(const std::string& text, SyncFields& out);

// Time to give a field that was just changed here.
int64_t syncChangeTime(int64_t previous, bool clockSet, int64_t now);

}  // namespace thermo
