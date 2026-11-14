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
static uint8_t s_drawBuf[kWidth * 40 * 2];  // 40 lines of RGB565

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

  touchBegin();

  lv_init();
  lv_tick_set_cb(tickCb);

  lv_display_t* disp = lv_display_create(kWidth, kHeight);
  lv_display_set_flush_cb(disp, flushCb);
  lv_display_set_buffers(disp, s_drawBuf, nullptr, sizeof(s_drawBuf),
                         LV_DISPLAY_RENDER_MODE_PARTIAL);

  lv_indev_t* indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touchCb);
}

void loop() { lv_timer_handler(); }

}  // namespace display
