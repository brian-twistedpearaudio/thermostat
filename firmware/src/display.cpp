#include "display.h"

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <Wire.h>
#include <lvgl.h>

#include "config.h"
#include "pins.h"

namespace display {

static const int kWidth = 320;   // landscape
static const int kHeight = 170;
static const uint8_t kTouchAddr = 0x15;  // CST816

static TFT_eSPI s_tft;
// LVGL 9 asserts that draw buffers are aligned; a plain uint8_t array is not guaranteed to be.
alignas(4) static uint8_t s_drawBuf[kWidth * 40 * 2];  // 40 lines of RGB565

static uint32_t tickCb() { return millis(); }

static void flushCb(lv_display_t* disp, const lv_area_t* area, uint8_t* px) {
  const uint32_t w = lv_area_get_width(area);
  const uint32_t h = lv_area_get_height(area);
  s_tft.startWrite();
  s_tft.setAddrWindow(area->x1, area->y1, w, h);
  s_tft.pushColors(reinterpret_cast<uint16_t*>(px), w * h, true);
  s_tft.endWrite();
  lv_display_flush_ready(disp);
}

// ---- Backlight ----
// Written for the AW9364 backlight driver (VERIFY ON HARDWARE): 16 levels. Each short LOW
// pulse steps down one level, wrapping from 1 to 16; held LOW over 2.5 ms it resets. Not PWM.
static const int kLevels = 16;
// Backlight step for 10%, 20% ... 100%. Not linear: the eye sees 1/16 of full light as much
// brighter than 6%, so the low settings are bunched at the dim end (about pct^1.7).
static const uint8_t kLevelForTenth[10] = {1, 1, 2, 3, 5, 7, 9, 11, 13, 16};
// 10% also gets a black layer over the image, to go below the backlight's lowest step.
// Raise this to make 10% (and the idle dim) darker, lower it to make it lighter.
static const lv_opa_t kShadeOpa = LV_OPA_40;
static const int kDimPct = 10;  // idle brightness; same level as the 10% Brightness setting
static portMUX_TYPE s_blMux = portMUX_INITIALIZER_UNLOCKED;
static int s_level = kLevels;
static int s_awakePct = 100;
static lv_obj_t* s_shade = nullptr;
static uint32_t s_dimAfterMs = 0;  // 0 = never
static uint32_t s_lastTouchMs = 0;
static bool s_dimmed = false;
static bool s_swallowTouch = false;  // the touch that woke the screen does nothing else
static bool s_wakePending = false;
static void (*s_onDim)() = nullptr;
static void (*s_onWake)() = nullptr;

static void backlightBegin() {
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, LOW);
  delay(5);  // known state, even after a warm reset
  digitalWrite(TFT_BL, HIGH);
  delayMicroseconds(50);
  s_level = kLevels;
}

static void setLevel(int level) {
  const int pulses = (s_level - level + kLevels) % kLevels;
  for (int i = 0; i < pulses; ++i) {
    portENTER_CRITICAL(&s_blMux);  // an interrupted pulse could be read as a reset
    digitalWrite(TFT_BL, LOW);
    delayMicroseconds(2);
    digitalWrite(TFT_BL, HIGH);
    portEXIT_CRITICAL(&s_blMux);
    delayMicroseconds(2);
  }
  s_level = level;
}

static void showPct(int pct) {
  int tenth = (pct + 5) / 10;
  if (tenth < 1) tenth = 1;
  if (tenth > 10) tenth = 10;
  setLevel(kLevelForTenth[tenth - 1]);
  lv_obj_set_hidden(s_shade, tenth != 1);
}

static void shadeBegin() {
  s_shade = lv_obj_create(lv_layer_top());  // above every screen
  lv_obj_remove_style_all(s_shade);
  lv_obj_set_size(s_shade, kWidth, kHeight);
  lv_obj_set_style_bg_color(s_shade, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(s_shade, kShadeOpa, 0);
  lv_obj_set_clickable(s_shade, false);  // touches pass through
  lv_obj_set_scrollable(s_shade, false);
  lv_obj_set_hidden(s_shade, true);
}

void setBrightness(int pct) {
  s_awakePct = pct;
  if (!s_dimmed) showPct(pct);
}

void setDimTimeout(int seconds) { s_dimAfterMs = static_cast<uint32_t>(seconds) * 1000; }
void onDim(void (*cb)()) { s_onDim = cb; }
void onWake(void (*cb)()) { s_onWake = cb; }

// Reads one touch point in the panel's native portrait coordinates (170 x 320).
static bool readTouchRaw(uint16_t& x, uint16_t& y) {
  Wire.beginTransmission(kTouchAddr);
  Wire.write(0x02);  // finger count, then X high/low, Y high/low
  if (Wire.endTransmission(false) != 0) return false;  // asleep or absent
  if (Wire.requestFrom(kTouchAddr, static_cast<uint8_t>(5)) != 5) return false;
  const uint8_t fingers = Wire.read();
  const uint8_t xh = Wire.read();
  const uint8_t xl = Wire.read();
  const uint8_t yh = Wire.read();
  const uint8_t yl = Wire.read();
  if (fingers == 0) return false;
  x = static_cast<uint16_t>((xh & 0x0F) << 8) | xl;
  y = static_cast<uint16_t>((yh & 0x0F) << 8) | yl;
  return true;
}

static void touchCb(lv_indev_t*, lv_indev_data_t* data) {
  uint16_t rx, ry;
  if (!readTouchRaw(rx, ry)) {
    s_swallowTouch = false;
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }
  s_lastTouchMs = millis();
  if (s_dimmed) {
    s_dimmed = false;
    s_swallowTouch = true;
    s_wakePending = true;  // finished in loop(), outside LVGL input handling
  }
  if (s_swallowTouch) {
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }
  // Map portrait panel coordinates to the landscape screen.
  // VERIFY ON HARDWARE: if touches land mirrored, swap the two branches.
  int x, y;
#if DISPLAY_ROTATION == 1
  x = ry;
  y = (kHeight - 1) - rx;
#else
  x = (kWidth - 1) - ry;
  y = rx;
#endif
  if (x < 0) x = 0;
  if (x > kWidth - 1) x = kWidth - 1;
  if (y < 0) y = 0;
  if (y > kHeight - 1) y = kHeight - 1;
  data->point.x = x;
  data->point.y = y;
  data->state = LV_INDEV_STATE_PRESSED;
}

static void touchBegin() {
  pinMode(PIN_TOUCH_INT, INPUT);
  pinMode(PIN_TOUCH_RST, OUTPUT);
  digitalWrite(PIN_TOUCH_RST, LOW);
  delay(10);
  digitalWrite(PIN_TOUCH_RST, HIGH);
  delay(50);
  // Keep the controller awake so it always answers on I2C.
  Wire.beginTransmission(kTouchAddr);
  Wire.write(0xFE);
  Wire.write(0x01);
  Wire.endTransmission();
}

void begin() {
  pinMode(PIN_LCD_POWER, OUTPUT);
  digitalWrite(PIN_LCD_POWER, HIGH);

  s_tft.init();
  s_tft.setRotation(DISPLAY_ROTATION);
  s_tft.fillScreen(TFT_BLACK);
  backlightBegin();

  touchBegin();

  lv_init();
  lv_tick_set_cb(tickCb);

  lv_display_t* disp = lv_display_create(kWidth, kHeight);
  lv_display_set_flush_cb(disp, flushCb);
  lv_display_set_buffers(disp, s_drawBuf, nullptr, sizeof(s_drawBuf),
                         LV_DISPLAY_RENDER_MODE_PARTIAL);

  shadeBegin();

  lv_indev_t* indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touchCb);
}

void loop() {
  lv_timer_handler();
  if (s_wakePending) {
    s_wakePending = false;
    if (s_onWake) s_onWake();
    showPct(s_awakePct);
  }
  if (!s_dimmed && s_dimAfterMs != 0 && millis() - s_lastTouchMs >= s_dimAfterMs) {
    s_dimmed = true;
    showPct(kDimPct);
    if (s_onDim) s_onDim();
  }
}

}  // namespace display
