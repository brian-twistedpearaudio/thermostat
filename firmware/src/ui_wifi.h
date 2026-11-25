// Wi-Fi screen: connection status, and the hotspot QR code during setup.
#pragma once
#include "net.h"

namespace ui {

void wifiCreate(void (*onBack)());
void wifiUpdate(const net::Status& s, const char* serverText);  // serverText is shown when connected
void wifiShow();
const char* wifiSummary(const net::Status& s);  // short text for the settings list

}  // namespace ui
