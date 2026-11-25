// Sends 5-minute records to the server and fetches history from it. Runs in its own task, so heating control and the setup
// page never wait for it. While the server is not answering, records are kept in flash (the
// "spiffs" partition, used directly) and sent, oldest first, when it is back.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "graph_data.h"
#include "log_interval.h"

namespace uplink {

void begin();
void add(const thermo::LogRecord& rec);  // returns at once; dropped if no server is set

// One line for the screen; empty if no server is set.
void summary(char* buf, size_t n);

// ---- History for the Graph screen ----

enum class HistoryState : uint8_t { Ok, NoServer, NoWifi, NoClock, NoAnswer, BadToken, NoRecords, OldServer };

struct History {
  HistoryState state = HistoryState::NoServer;
  thermo::GraphSpan span;
  thermo::GraphPoint points[thermo::kGraphPoints];  // valid when Ok
};

// Asks for the span ending now, with this many seconds per point. Returns at once; a newer
// request replaces one still waiting.
void requestHistory(uint32_t bucket);

// True once for each answer, which is copied to `out`. An answer to a replaced request is not given.
bool takeHistory(History& out);

}  // namespace uplink
