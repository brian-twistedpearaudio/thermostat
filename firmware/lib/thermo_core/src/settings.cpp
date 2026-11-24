#include "settings.h"

#include <stdio.h>
#include <stdlib.h>

#include "control.h"

namespace thermo {

static const int kSwingMin = 5, kSwingMax = 30, kSwingStep = 5;
static const int kOffsetMax = 100;
static const int kBrightMin = 10, kBrightMax = 100, kBrightStep = 10;
static const int kDimChoices[] = {15, 30, 60, 120, 300, 0};  // 0 = never, after the longest
static const int kDimCount = sizeof(kDimChoices) / sizeof(kDimChoices[0]);

static int clampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static int dimIndex(int seconds) {
  for (int i = 0; i < kDimCount; ++i) {
    if (kDimChoices[i] == seconds) return i;
  }
  return -1;
}

void sanitize(Settings& s) {
  const Settings def;
  s.setpointF = clampInt(s.setpointF, static_cast<int>(kMinSetpointF), static_cast<int>(kMaxSetpointF));
  s.swingTenths = clampInt(s.swingTenths, kSwingMin, kSwingMax) / kSwingStep * kSwingStep;
  s.offsetTenths = clampInt(s.offsetTenths, -kOffsetMax, kOffsetMax);
  s.brightnessPct = clampInt(s.brightnessPct, kBrightMin, kBrightMax) / kBrightStep * kBrightStep;
  if (dimIndex(s.dimSeconds) < 0) s.dimSeconds = def.dimSeconds;
}

static bool assign(int& field, int value) {
  if (field == value) return false;
  field = value;
  return true;
}

bool stepSetting(Settings& s, SettingId id, int dir) {
  const int d = dir > 0 ? 1 : -1;
  switch (id) {
    case SettingId::Mode:
      s.heatMode = !s.heatMode;
      return true;
    case SettingId::Swing:
      return assign(s.swingTenths, clampInt(s.swingTenths + d * kSwingStep, kSwingMin, kSwingMax));
    case SettingId::Offset:
      return assign(s.offsetTenths, clampInt(s.offsetTenths + d, -kOffsetMax, kOffsetMax));
    case SettingId::Brightness:
      return assign(s.brightnessPct, clampInt(s.brightnessPct + d * kBrightStep, kBrightMin, kBrightMax));
    case SettingId::DimTimeout:
      return assign(s.dimSeconds, kDimChoices[clampInt(dimIndex(s.dimSeconds) + d, 0, kDimCount - 1)]);
  }
  return false;
}

void formatSetting(const Settings& s, SettingId id, char* buf, size_t n) {
  switch (id) {
    case SettingId::Mode:
      snprintf(buf, n, "%s", s.heatMode ? "Heat" : "Off");
      break;
    case SettingId::Swing:
      snprintf(buf, n, "%d.%d\xC2\xB0", s.swingTenths / 10, s.swingTenths % 10);
      break;
    case SettingId::Offset: {
      const int a = abs(s.offsetTenths);
      snprintf(buf, n, "%s%d.%d\xC2\xB0", s.offsetTenths < 0 ? "-" : (a ? "+" : ""), a / 10, a % 10);
      break;
    }
    case SettingId::Brightness:
      snprintf(buf, n, "%d%%", s.brightnessPct);
      break;
    case SettingId::DimTimeout:
      if (s.dimSeconds == 0) snprintf(buf, n, "Never");
      else if (s.dimSeconds < 60) snprintf(buf, n, "%d sec", s.dimSeconds);
      else snprintf(buf, n, "%d min", s.dimSeconds / 60);
      break;
  }
}

}  // namespace thermo
