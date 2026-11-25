// Text for talking to the server. No hardware calls: testable on the PC.
#pragma once
#include <string>
#include <vector>

#include "log_interval.h"

namespace thermo {

static const size_t kMaxServer = 63;
static const size_t kMaxToken = 64;

// Tidies an entered server address to "host:port" (port 5080 if not given). Empty is allowed: no
// server. A leading http:// and a trailing / are removed. False if it is not valid; `s` is then
// unchanged.
bool cleanServer(std::string& s);

// Letters and digits, 16..kMaxToken.
bool validToken(const std::string& t);

// Body of POST /api/v1/readings.
std::string uploadJson(const std::string& device, const std::string& name, const std::vector<LogRecord>& records);

}  // namespace thermo
