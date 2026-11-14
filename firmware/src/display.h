// LCD (TFT_eSPI), LVGL setup and CST816 touch input.
#pragma once

namespace display {
void begin();   // Wire must already be started
void loop();    // call often from loop(); runs LVGL
}  // namespace display
