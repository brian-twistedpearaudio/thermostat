#include "ui_idle.h"

#include <lvgl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "config.h"
#include "fonts/ui_fonts.h"

namespace ui {

static const uint32_t kBg = 0x000000;
static const uint32_t kText = 0xFFFFFF;
static const uint32_t kFaultText = 0xFF6060;  // temperature colour while the sensor is faulty

static const int kW = 320;
static const int kTempBaseline = 78;
static const int kTimeBaseline = 142;

static lv_obj_t* s_screen;
static lv_obj_t* s_whole;
static lv_obj_t* s_frac;
static lv_obj_t* s_time;

static int yForBaseline(const lv_font_t* f, int baseline) {
  return baseline - (lv_font_get_line_height(f) - f->base_line);
}

static lv_obj_t* makeLabel(const lv_font_t* font) {
  lv_obj_t* l = lv_label_create(s_screen);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(kText), 0);
  lv_label_set_text(l, "");
  return l;
}

void idleCreate() {
  s_screen = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(s_screen, lv_color_hex(kBg), 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_set_scrollable(s_screen, false);

  s_whole = makeLabel(&ui_font_temp_60);
  lv_obj_set_style_text_letter_space(s_whole, -2, 0);
  s_frac = makeLabel(&ui_font_frac_32);

  s_time = makeLabel(&lv_font_montserrat_36);
  lv_obj_set_width(s_time, kW);
  lv_obj_set_style_text_align(s_time, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(s_time, 0, yForBaseline(&lv_font_montserrat_36, kTimeBaseline));
}

void idleUpdate(const HomeState& s) {
  char buf[16];

  // Same value as the home screen. This screen has no fault box, so a fault turns it red.
  const long tenths = lroundf((s.sensorOk ? s.tempF : NO_SENSOR_DISPLAY_TEMP_F) * 10.0f);
  snprintf(buf, sizeof(buf), "%ld", tenths / 10);
  lv_label_set_text(s_whole, buf);
  snprintf(buf, sizeof(buf), ".%ld\xC2\xB0", labs(tenths % 10));
  lv_label_set_text(s_frac, buf);
  const lv_color_t color = lv_color_hex(s.sensorOk ? kText : kFaultText);
  lv_obj_set_style_text_color(s_whole, color, 0);
  lv_obj_set_style_text_color(s_frac, color, 0);

  // Centre the whole part and the fraction together.
  lv_obj_update_layout(s_screen);
  const int wholeW = lv_obj_get_width(s_whole) - 2;
  const int x = (kW - wholeW - lv_obj_get_width(s_frac)) / 2;
  lv_obj_set_pos(s_whole, x, yForBaseline(&ui_font_temp_60, kTempBaseline));
  lv_obj_set_pos(s_frac, x + wholeW, yForBaseline(&ui_font_frac_32, kTempBaseline));

  if (s.timeValid) {
    const int h12 = (s.hour % 12 == 0) ? 12 : s.hour % 12;
    snprintf(buf, sizeof(buf), "%d:%02d %s", h12, s.minute, s.hour < 12 ? "AM" : "PM");
    lv_label_set_text(s_time, buf);
  } else {
    lv_label_set_text(s_time, NO_TIME_DISPLAY_TEXT);
  }
}

void idleShow() { lv_screen_load(s_screen); }

}  // namespace ui
