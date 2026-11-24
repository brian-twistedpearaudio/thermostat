// Idle screen, shown while the display is dimmed: just the temperature and the time.
#pragma once
#include "ui_home.h"

namespace ui {

void idleCreate();
void idleUpdate(const HomeState& s);
void idleShow();

}  // namespace ui
