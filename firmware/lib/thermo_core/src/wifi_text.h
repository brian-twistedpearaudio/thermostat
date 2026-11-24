// Checks and text handling for Wi-Fi setup. No hardware calls: testable on the PC.
#pragma once
#include <string>

namespace thermo {

// SSID 1..32 bytes. Password empty (open network) or 8..63 printable ASCII characters.
bool validWifi(const std::string& ssid, const std::string& password);

// Makes text safe inside HTML and double- or single-quoted attributes.
std::string htmlEscape(const std::string& s);

}  // namespace thermo
