// Settings screen: a scrolling list, one setting per row.
#pragma once
#include "settings.h"

namespace ui {

// onStep is called with +1 / -1 when a setting is changed by touch.
void settingsCreate(void (*onStep)(thermo::SettingId id, int dir), void (*onBack)());
void settingsUpdate(const thermo::Settings& s);
void settingsShow();

}  // namespace ui
