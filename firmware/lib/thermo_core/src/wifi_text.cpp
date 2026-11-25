#include "wifi_text.h"

namespace thermo {

bool validWifi(const std::string& ssid, const std::string& password) {
  if (ssid.empty() || ssid.size() > 32) return false;
  if (ssid.find('\0') != std::string::npos) return false;
  if (password.empty()) return true;
  if (password.size() < 8 || password.size() > 63) return false;
  for (char c : password) {
    if (c < 0x20 || c > 0x7E) return false;
  }
  return true;
}

bool cleanName(std::string& name) {
  std::string out = name;
  for (const char* curly : {"\xE2\x80\x98", "\xE2\x80\x99"}) {  // U+2018, U+2019
    size_t at;
    while ((at = out.find(curly)) != std::string::npos) out.replace(at, 3, "'");
  }
  const size_t first = out.find_first_not_of(' ');
  out = first == std::string::npos ? "" : out.substr(first, out.find_last_not_of(' ') - first + 1);
  if (out.size() > kMaxName) return false;
  for (char c : out) {
    if (c < 0x20 || c > 0x7E) return false;
  }
  name = out;
  return true;
}

std::string htmlEscape(const std::string& s) {
  std::string out;
  for (char c : s) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      case '\'': out += "&#39;"; break;
      default: out += c;
    }
  }
  return out;
}

}  // namespace thermo
