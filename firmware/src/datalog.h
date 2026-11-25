// 5-minute records, passed to uplink, which sends them to the server.
#pragma once
#include <stdint.h>

#include "log_interval.h"

namespace datalog {

// Call about once a second with the current state. Does nothing until the time is known.
void tick(bool timeValid, uint32_t nowUtc, const thermo::LogSample& s);

}  // namespace datalog
