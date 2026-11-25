// Checks and text handling for Wi-Fi setup. No hardware calls: testable on the PC.
#pragma once
#include <string>

namespace thermo {

// SSID 1..32 bytes. Password empty (open network) or 8..63 printable ASCII characters.
bool validWifi(const std::string& ssid, const std::string& password);

// Thermostat name: 0..16 printable ASCII characters (what the Home screen font has).
static const size_t kMaxName = 16;

// Tidies an entered name: trims spaces and turns the curly apostrophes phones type into plain
// ones. False if the result is too long or has other characters; `name` is then unchanged.
bool cleanName(std::string& name);

// Makes text safe inside HTML and double- or single-quoted attributes.
std::string htmlEscape(const std::string& s);

}  // namespace thermo
