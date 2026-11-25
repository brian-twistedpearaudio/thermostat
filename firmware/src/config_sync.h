// Keeps the configuration in step with the server (docs/sync.md). Used only by the main loop; the
// calls to the server are made by uplink, in its own task.
#pragma once
#include "schedule.h"
#include "settings.h"
#include "sync_text.h"

namespace config_sync {

// Reads the change times and the schedule from flash. Call after net::begin() and uplink::begin().
void begin(const thermo::Settings& settings, thermo::Schedule& schedule);

// Call once a second. Notices changes made here and passes the configuration on to be sent.
void tick(const thermo::Settings& settings, const thermo::SyncState& state, bool clockSet);

// Takes a reply from the server, if there is one. `settings` and `schedule` are updated with what
// is new; true if anything changed. `setpoint` is then true if the set point was set, perhaps to
// the same value.
bool poll(thermo::Settings& settings, thermo::Schedule& schedule, bool clockSet, bool& setpoint);

}  // namespace config_sync
