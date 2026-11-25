// Home screen, design option 1 "Big Number".
#pragma once
#include <stdint.h>

namespace ui {

struct HomeState {
  bool sensorOk = false;
  float tempF = 0;
  bool hasHumidity = false;  // false with a BMP280
  float humidity = 0;
  float setpointF = 68;
  bool heating = false;
  bool modeOff = false;      // mode is Off: the set point is not in use
  bool overridden = false;   // manual change in force (label wording comes with the schedule UI)
  bool timeValid = false;
  uint8_t hour = 0;          // 0..23 local
  uint8_t minute = 0;
};

// onSetpointDelta is called with +1 or -1 when the user presses + or -.
// onSettings is called when the top-left of the screen (gear) is tapped.
// onGraph is called when the big temperature is tapped.
void homeCreate(void (*onSetpointDelta)(int delta), void (*onSettings)(), void (*onGraph)());
void homeShow();
void homeUpdate(const HomeState& s);
void homeSetName(const char* name);  // top-left text; empty shows "Inside"

}  // namespace ui
