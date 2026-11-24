// LCD (TFT_eSPI), LVGL setup and CST816 touch input.
#pragma once

namespace display {
void begin();   // Wire must already be started
void loop();    // call often from loop(); runs LVGL, dims the screen when idle
void setBrightness(int pct);       // used while awake
void setDimTimeout(int seconds);   // 0 = never dim
void onDim(void (*cb)());          // called when the screen dims
void onWake(void (*cb)());         // called when a touch wakes it
}  // namespace display
