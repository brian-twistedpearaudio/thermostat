// Sends 5-minute records to the server. Runs in its own task, so heating control and the setup
// page never wait for it. While the server is not answering, records are kept in flash (the
// "spiffs" partition, used directly) and sent, oldest first, when it is back.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "log_interval.h"

namespace uplink {

void begin();
void add(const thermo::LogRecord& rec);  // returns at once; dropped if no server is set

// One line for the screen; empty if no server is set.
void summary(char* buf, size_t n);

}  // namespace uplink
