// User settings: limits, steps and display text. No hardware calls: testable on the PC.
// Values are whole numbers (tenths where needed) so they store and compare exactly.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace thermo {

struct Settings {
  bool heatMode = true;       // false = Off
  int setpointF = 68;
  int swingTenths = 10;       // total band, 0.1 F
  int offsetTenths = 0;       // added to the sensor reading, 0.1 F
  int brightnessPct = 100;
  int dimSeconds = 60;        // 0 = never dim
};

enum class SettingId : uint8_t { Mode, Swing, Offset, Brightness, DimTimeout };

// Forces every value into its allowed range (used on values read from flash).
void sanitize(Settings& s);

// Moves one setting a step up (dir > 0) or down. Mode toggles. Returns true if it changed.
bool stepSetting(Settings& s, SettingId id, int dir);

// Text for the settings screen, e.g. "1.0°".
void formatSetting(const Settings& s, SettingId id, char* buf, size_t n);

}  // namespace thermo
