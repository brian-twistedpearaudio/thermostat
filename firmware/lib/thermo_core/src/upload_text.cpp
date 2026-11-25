#include "upload_text.h"

#include <stdio.h>

namespace thermo {

static bool isDigit(char c) { return c >= '0' && c <= '9'; }
static bool isAlnum(char c) { return isDigit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

bool cleanServer(std::string& s) {
  std::string out = s;
  while (!out.empty() && out.back() == ' ') out.pop_back();
  while (!out.empty() && out.front() == ' ') out.erase(0, 1);
  if (out.compare(0, 7, "http://") == 0) out.erase(0, 7);
  if (!out.empty() && out.back() == '/') out.pop_back();
  if (out.empty()) {
    s = out;
    return true;
  }

  size_t colon = out.find(':');
  if (colon == std::string::npos) {
    colon = out.size();
    out += ":5080";
  }
  if (colon == 0 || out.size() > kMaxServer) return false;
  for (size_t i = 0; i < colon; ++i) {
    if (!isAlnum(out[i]) && out[i] != '.' && out[i] != '-') return false;
  }
  const std::string port = out.substr(colon + 1);
  if (port.empty() || port.size() > 5) return false;
  unsigned long n = 0;
  for (char c : port) {
    if (!isDigit(c)) return false;
    n = n * 10 + static_cast<unsigned long>(c - '0');
  }
  if (n == 0 || n > 65535) return false;
  s = out;
  return true;
}

bool validToken(const std::string& t) {
  if (t.size() < 16 || t.size() > kMaxToken) return false;
  for (char c : t) {
    if (!isAlnum(c)) return false;
  }
  return true;
}

static std::string quoted(const std::string& s) {
  std::string out = "\"";
  for (char c : s) {
    if (c == '"' || c == '\\') out += '\\';
    out += c;
  }
  return out + "\"";
}

static std::string number(bool has, float v, const char* format) {
  if (!has) return "null";
  char buf[24];
  snprintf(buf, sizeof(buf), format, static_cast<double>(v));
  return buf;
}

std::string uploadJson(const std::string& device, const std::string& name, const std::vector<LogRecord>& records) {
  std::string j = "{\"device\":" + quoted(device) + ",\"name\":" + quoted(name) + ",\"readings\":[";
  for (size_t i = 0; i < records.size(); ++i) {
    const LogRecord& r = records[i];
    if (i > 0) j += ",";
    j += "{\"time\":" + std::to_string(r.time);
    j += ",\"tempF\":" + number(r.hasTemp, r.tempF, "%.2f");
    j += ",\"humidity\":" + number(r.hasHumidity, r.humidity, "%.1f");
    j += ",\"pressureHpa\":" + number(r.hasTemp, r.pressureHpa, "%.1f");
    j += ",\"setpointF\":" + number(true, r.setpointF, "%.1f");
    j += ",\"heatPct\":" + std::to_string(r.heatPct);
    j += std::string(",\"sensorFault\":") + (r.sensorFault ? "true" : "false");
    j += std::string(",\"modeOff\":") + (r.modeOff ? "true" : "false");
    j += std::string(",\"partial\":") + (r.partial ? "true" : "false") + "}";
  }
  return j + "]}";
}

}  // namespace thermo
