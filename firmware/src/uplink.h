// Sends 5-minute records to the server, syncs the configuration and fetches history. Runs in its own task, so heating control and the setup
// page never wait for it. While the server is not answering, records are kept in flash (the
// "spiffs" partition, used directly) and sent, oldest first, when it is back.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include <string>

#include "graph_data.h"
#include "log_interval.h"
#include "sync_text.h"

namespace uplink {

void begin();
void add(const thermo::LogRecord& rec);  // returns at once; dropped if no server is set

// One line for the screen; empty if no server is set.
void summary(char* buf, size_t n);

// Short problem text for the Home status line; empty if none.
const char* notice();

// ---- Configuration sync (see config_sync) ----

struct SyncRequest {
  std::string text;
  uint32_t changes[thermo::kSyncCount];  // given back with the reply
};

struct SyncReply {
  thermo::SyncFields fields;
  uint32_t changes[thermo::kSyncCount];
};

// The newest request to send. A call is made every 10 s, or at once if `soon`.
void offerSync(const SyncRequest& r, bool soon);

// True once for each good reply.
bool takeSync(SyncReply& out);

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
