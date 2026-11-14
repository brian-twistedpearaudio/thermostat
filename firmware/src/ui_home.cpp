#include "ui_home.h"

#include <lvgl.h>
#include <math.h>
#include <stdio.h>

namespace ui {

// Palette (matches the design canvas)
static const uint32_t kBg = 0x0B0F14;
static const uint32_t kSurface = 0x18202A;
static const uint32_t kSurfacePressed = 0x2A3644;
static const uint32_t kText = 0xF2F5F8;
static const uint32_t kMuted = 0x9AA7B4;
static const uint32_t kHeatText = 0xFFB37A;
static const uint32_t kHeatBg = 0x3A2210;
static const uint32_t kFaultText = 0xFFB4B4;
static const uint32_t kFaultBg = 0x3A1518;

// Layout, real pixels on the 320 x 170 screen
static const int kPad = 14;
static const int kRightX = 202;
static const int kRightW = 104;
static const int kBtnH = 46;

static lv_obj_t* s_clock;
static lv_obj_t* s_whole;
static lv_obj_t* s_frac;
static lv_obj_t* s_humidity;
static lv_obj_t* s_pill;
static lv_obj_t* s_pillLabel;
static lv_obj_t* s_setCaption;
static lv_obj_t* s_setValue;
static void (*s_onDelta)(int) = nullptr;

static lv_obj_t* makeLabel(lv_obj_t* parent, const lv_font_t* font, uint32_t color, const char* text) {
  lv_obj_t* l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  lv_label_set_text(l, text);
  return l;
}

static void buttonEvent(lv_event_t* e) {
  const int delta = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  if (s_onDelta) s_onDelta(delta);
}

static void makeButton(lv_obj_t* parent, int y, const char* symbol, int delta) {
  lv_obj_t* b = lv_button_create(parent);
  lv_obj_set_pos(b, kRightX, y);
  lv_obj_set_size(b, kRightW, kBtnH);
  lv_obj_set_style_radius(b, 12, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(kSurface), 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(kSurfacePressed), LV_STATE_PRESSED);
  void* ud = reinterpret_cast<void*>(static_cast<intptr_t>(delta));
  lv_obj_add_event_cb(b, buttonEvent, LV_EVENT_SHORT_CLICKED, ud);
  lv_obj_add_event_cb(b, buttonEvent, LV_EVENT_LONG_PRESSED_REPEAT, ud);  // hold to keep stepping
  lv_obj_t* l = makeLabel(b, &lv_font_montserrat_28, kText, symbol);
  lv_obj_center(l);
}

void homeCreate(void (*onSetpointDelta)(int delta)) {
  s_onDelta = onSetpointDelta;

  lv_obj_t* scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(kBg), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  // Left column: label + clock, big temperature, humidity + status pill
  lv_obj_t* inside = makeLabel(scr, &lv_font_montserrat_14, kMuted, "Inside");
  lv_obj_set_pos(inside, kPad, kPad);

  s_clock = makeLabel(scr, &lv_font_montserrat_14, kMuted, "--:--");
  lv_obj_set_width(s_clock, 90);
  lv_obj_set_style_text_align(s_clock, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_pos(s_clock, kRightX - 12 - 90, kPad);

  s_whole = makeLabel(scr, &lv_font_montserrat_48, kText, "--");
  lv_obj_set_pos(s_whole, kPad, 52);
  s_frac = makeLabel(scr, &lv_font_montserrat_28, kMuted, "\xC2\xB0");

  s_humidity = makeLabel(scr, &lv_font_montserrat_14, kMuted, "");
  lv_obj_set_pos(s_humidity, kPad, 138);

  s_pill = lv_obj_create(scr);
  lv_obj_remove_flag(s_pill, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(s_pill, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_hor(s_pill, 8, 0);
  lv_obj_set_style_pad_ver(s_pill, 3, 0);
  lv_obj_set_style_radius(s_pill, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(s_pill, 0, 0);
  lv_obj_set_style_bg_color(s_pill, lv_color_hex(kHeatBg), 0);
  s_pillLabel = makeLabel(s_pill, &lv_font_montserrat_14, kHeatText, "Heating");
  lv_obj_add_flag(s_pill, LV_OBJ_FLAG_HIDDEN);

  // Right column: +, set point, -
  makeButton(scr, kPad, LV_SYMBOL_PLUS, +1);
  makeButton(scr, 170 - kPad - kBtnH, LV_SYMBOL_MINUS, -1);

  s_setCaption = makeLabel(scr, &lv_font_montserrat_14, kMuted, "SET");
  lv_obj_set_width(s_setCaption, kRightW);
  lv_obj_set_style_text_align(s_setCaption, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(s_setCaption, kRightX, 62);

  s_setValue = makeLabel(scr, &lv_font_montserrat_28, kHeatText, "--\xC2\xB0");
  lv_obj_set_width(s_setValue, kRightW);
  lv_obj_set_style_text_align(s_setValue, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(s_setValue, kRightX, 77);
}

void homeUpdate(const HomeState& s) {
  char buf[24];

  // Clock
  if (s.timeValid) {
    const int h12 = (s.hour % 12 == 0) ? 12 : s.hour % 12;
    snprintf(buf, sizeof(buf), "%d:%02d %s", h12, s.minute, s.hour < 12 ? "AM" : "PM");
    lv_label_set_text(s_clock, buf);
  } else {
    lv_label_set_text(s_clock, "--:--");
  }

  // Temperature, e.g. "68" + ".4°"
  if (s.sensorOk) {
    const long tenths = lroundf(s.tempF * 10.0f);
    snprintf(buf, sizeof(buf), "%ld", tenths / 10);
    lv_label_set_text(s_whole, buf);
    snprintf(buf, sizeof(buf), ".%ld\xC2\xB0", labs(tenths % 10));
    lv_label_set_text(s_frac, buf);
    lv_obj_set_style_text_color(s_whole, lv_color_hex(kText), 0);
    snprintf(buf, sizeof(buf), "%d%% humidity", static_cast<int>(lroundf(s.humidity)));
    lv_label_set_text(s_humidity, buf);
  } else {
    lv_label_set_text(s_whole, "--");
    lv_label_set_text(s_frac, "\xC2\xB0");
    lv_obj_set_style_text_color(s_whole, lv_color_hex(kMuted), 0);
    lv_label_set_text(s_humidity, "");
  }
  lv_obj_update_layout(s_whole);
  lv_obj_align_to(s_frac, s_whole, LV_ALIGN_OUT_RIGHT_BOTTOM, 2, -6);

  // Status pill: sensor fault, heating, or hidden
  if (!s.sensorOk) {
    lv_label_set_text(s_pillLabel, "Sensor fault - heat off");
    lv_obj_set_style_text_color(s_pillLabel, lv_color_hex(kFaultText), 0);
    lv_obj_set_style_bg_color(s_pill, lv_color_hex(kFaultBg), 0);
    lv_obj_remove_flag(s_pill, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(s_pill, kPad, 134);
  } else if (s.heating) {
    lv_label_set_text(s_pillLabel, "Heating");
    lv_obj_set_style_text_color(s_pillLabel, lv_color_hex(kHeatText), 0);
    lv_obj_set_style_bg_color(s_pill, lv_color_hex(kHeatBg), 0);
    lv_obj_remove_flag(s_pill, LV_OBJ_FLAG_HIDDEN);
    lv_obj_update_layout(s_humidity);
    lv_obj_align_to(s_pill, s_humidity, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
  } else {
    lv_obj_add_flag(s_pill, LV_OBJ_FLAG_HIDDEN);
  }

  // Set point
  snprintf(buf, sizeof(buf), "%d\xC2\xB0", static_cast<int>(lroundf(s.setpointF)));
  lv_label_set_text(s_setValue, buf);
  lv_obj_set_style_text_color(s_setValue, lv_color_hex(s.sensorOk ? kHeatText : kMuted), 0);
  lv_label_set_text(s_setCaption, s.overridden ? "MANUAL" : "SET");
}

}  // namespace ui
