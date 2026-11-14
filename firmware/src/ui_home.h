// Home screen, design option 1 "Big Number".
#pragma once
#include <stdint.h>

namespace ui {

struct HomeState {
  bool sensorOk = false;
  float tempF = 0;
  float humidity = 0;
  float setpointF = 68;
  bool heating = false;
  bool overridden = false;   // manual change in force (label wording comes with the schedule UI)
  bool timeValid = false;
  uint8_t hour = 0;          // 0..23 local
  uint8_t minute = 0;
};

// onSetpointDelta is called with +1 or -1 when the user presses + or -.
void homeCreate(void (*onSetpointDelta)(int delta));
void homeUpdate(const HomeState& s);

}  // namespace ui
