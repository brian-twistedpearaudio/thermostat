// Boiler call relay (Omron G6K-2P-DC5 through a transistor driver).
#pragma once

namespace relay {
void begin();        // drives the pin LOW (relay released) before making it an output
void set(bool on);   // true = call for heat
bool isOn();
}  // namespace relay
