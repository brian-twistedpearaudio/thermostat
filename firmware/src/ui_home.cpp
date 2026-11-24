// Home screen, design option 1 "Big Number".
// Sizes and positions follow the design canvas, which is drawn at 2x of the 320 x 170 screen:
// every canvas measurement is halved here. Text is placed by its baseline so it lands where
// the browser puts it in the canvas.
#include "ui_home.h"

#include "config.h"
#include "fonts/ui_fonts.h"

#include <lvgl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

namespace ui {

// Palette, tuned for the T-Display-S3 panel. The canvas values (in comments) are
// near-black with a slight tint; this panel shows those as navy / purple, so the
// darks here are neutral and the tinted ones more saturated.
static const uint32_t kBg = 0x000000;              // canvas 0x0B0F14
static const uint32_t kSurface = 0x1A1A1A;         // canvas 0x18202A
static const uint32_t kSurfacePressed = 0x333333;  // canvas 0x2A3644
static const uint32_t kText = 0xFFFFFF;            // canvas 0xF2F5F8
static const uint32_t kMuted = 0x9A9A9A;           // canvas 0x9AA7B4
static const uint32_t kHeatText = 0xFFA050;        // canvas 0xFFB37A
static const uint32_t kHeatBg = 0x4A2000;          // canvas 0x3A2210
static const uint32_t kHeatDot = 0xFF8A3D;         // canvas 0xFF8A3D
static const uint32_t kFaultText = 0xFFC0C0;       // canvas 0xFFB4B4
static const uint32_t kFaultBg = 0x700000;         // canvas 0x3A1518

// Layout, real pixels on the 320 x 170 screen
static const int kPad = 14;                       // canvas padding 28
static const int kLeftW = 176;                    // left column, x 14..190
static const int kRightX = 202;                   // right column, x 202..306 (gap 24 -> 12)
static const int kRightW = 104;                   // canvas 208
static const int kBtnH = 48;                      // canvas 96
static const int kBtnRadius = 12;                 // canvas 24

// Baselines (y of the text baseline), worked out from the canvas flex layout
static const int kTopBaseline = 25;      // "Inside" and the clock
static const int kTempBaseline = 109;    // big temperature and its ".4°"
static const int kBottomBaseline = 150;  // humidity and the Heating pill text
static const int kCaptionBaseline = 74;  // SET / MANUAL
static const int kSetBaseline = 99;      // set point value

static lv_obj_t* s_clock;
static lv_obj_t* s_whole;
static lv_obj_t* s_frac;
static lv_obj_t* s_humidity;
static lv_obj_t* s_heatPill;
static lv_obj_t* s_faultBox;
static lv_obj_t* s_setCaption;
static lv_obj_t* s_setValue;
static lv_obj_t* s_screen;
static void (*s_onDelta)(int) = nullptr;
static void (*s_onSettings)() = nullptr;

// y for a label so that its first line's baseline sits at `baseline`.
static int yForBaseline(const lv_font_t* f, int baseline) {
  return baseline - (lv_font_get_line_height(f) - f->base_line);
}

static lv_obj_t* makeLabel(lv_obj_t* parent, const lv_font_t* font, uint32_t color, const char* text) {
  lv_obj_t* l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  lv_label_set_text(l, text);
  return l;
}

// A plain rectangle that ignores touches (used for icon strokes and dots).
static lv_obj_t* makeShape(lv_obj_t* parent, int w, int h, uint32_t color, int radius) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(o, radius, 0);
  lv_obj_set_clickable(o, false);
  return o;
}

static void buttonEvent(lv_event_t* e) {
  const int delta = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  if (s_onDelta) s_onDelta(delta);
}

// + / - button. The icon is drawn as rounded bars: canvas stroke 5 -> 3 px, length 28 -> 14 px.
static void makeButton(lv_obj_t* parent, int y, bool plus, int delta) {
  lv_obj_t* b = lv_button_create(parent);
  lv_obj_remove_style_all(b);
  lv_obj_set_pos(b, kRightX, y);
  lv_obj_set_size(b, kRightW, kBtnH);
  lv_obj_set_style_radius(b, kBtnRadius, 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(kSurface), 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(kSurfacePressed), LV_STATE_PRESSED);
  void* ud = reinterpret_cast<void*>(static_cast<intptr_t>(delta));
  lv_obj_add_event_cb(b, buttonEvent, LV_EVENT_SHORT_CLICKED, ud);
  lv_obj_add_event_cb(b, buttonEvent, LV_EVENT_LONG_PRESSED_REPEAT, ud);  // hold to keep stepping

  lv_obj_t* h = makeShape(b, 14, 3, kText, 2);
  lv_obj_center(h);
  if (plus) {
    lv_obj_t* v = makeShape(b, 3, 14, kText, 2);
    lv_obj_center(v);
  }
}

static void settingsEvent(lv_event_t*) {
  if (s_onSettings) s_onSettings();
}

void homeShow() { lv_screen_load(s_screen); }

void homeCreate(void (*onSetpointDelta)(int delta), void (*onSettings)()) {
  s_onDelta = onSetpointDelta;
  s_onSettings = onSettings;

  lv_obj_t* scr = lv_screen_active();
  s_screen = scr;
  lv_obj_set_style_bg_color(scr, lv_color_hex(kBg), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_scrollable(scr, false);

  // ---- Left column ----
  // Top row: "Inside" left, clock right (canvas 22 px / 500 -> 11 px Medium, muted)
  lv_obj_t* inside = makeLabel(scr, &ui_font_body_11, kMuted, "Inside");
  lv_obj_set_pos(inside, kPad, yForBaseline(&ui_font_body_11, kTopBaseline));

  s_clock = makeLabel(scr, &ui_font_body_11, kMuted, "--:--");
  lv_obj_set_width(s_clock, 90);
  lv_obj_set_style_text_align(s_clock, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_pos(s_clock, kPad + kLeftW - 90, yForBaseline(&ui_font_body_11, kTopBaseline));

  // Settings: a gear after "Inside"; the touch target is the whole top-left strip.
  lv_obj_update_layout(inside);
  lv_obj_t* gear = makeLabel(scr, &lv_font_montserrat_14, kMuted, LV_SYMBOL_SETTINGS);
  lv_obj_set_pos(gear, kPad + lv_obj_get_width(inside) + 8, 13);
  lv_obj_t* hit = lv_obj_create(scr);
  lv_obj_remove_style_all(hit);
  lv_obj_set_pos(hit, 0, 0);
  lv_obj_set_size(hit, kPad + kLeftW, 48);
  lv_obj_set_scrollable(hit, false);
  lv_obj_add_event_cb(hit, settingsEvent, LV_EVENT_CLICKED, nullptr);

  // Big temperature: whole part 150 px / 600 -> 75 px SemiBold, letter-spacing -4 -> -2;
  // ".4°" 64 px -> 32 px SemiBold, muted, on the same baseline.
  s_whole = makeLabel(scr, &ui_font_temp_75, kText, "--");
  lv_obj_set_style_text_letter_space(s_whole, -2, 0);
  lv_obj_set_pos(s_whole, kPad, yForBaseline(&ui_font_temp_75, kTempBaseline));
  s_frac = makeLabel(scr, &ui_font_frac_32, kMuted, "\xC2\xB0");

  // Bottom row: humidity text, then the Heating pill (canvas padding 6/14 -> 3/7, gap 16 -> 8)
  s_humidity = makeLabel(scr, &ui_font_body_11, kMuted, "");
  lv_obj_set_pos(s_humidity, kPad, yForBaseline(&ui_font_body_11, kBottomBaseline));

  s_heatPill = lv_obj_create(scr);
  lv_obj_remove_style_all(s_heatPill);
  lv_obj_set_clickable(s_heatPill, false);
  lv_obj_set_size(s_heatPill, LV_SIZE_CONTENT, 19);
  lv_obj_set_style_pad_hor(s_heatPill, 7, 0);
  lv_obj_set_style_radius(s_heatPill, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(s_heatPill, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(s_heatPill, lv_color_hex(kHeatBg), 0);
  lv_obj_set_flex_flow(s_heatPill, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(s_heatPill, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(s_heatPill, 4, 0);
  makeShape(s_heatPill, 6, 6, kHeatDot, LV_RADIUS_CIRCLE);
  makeLabel(s_heatPill, &ui_font_semi_11, kHeatText, "Heating");
  lv_obj_set_hidden(s_heatPill, true);

  // Sensor fault box: full column width, canvas padding 10/16 -> 5/8, radius 16 -> 8,
  // icon 28 -> 14 px, gap 12 -> 6, text 22 px / 600 -> 11 px SemiBold. Sits on the bottom edge.
  s_faultBox = lv_obj_create(scr);
  lv_obj_remove_style_all(s_faultBox);
  lv_obj_set_clickable(s_faultBox, false);
  lv_obj_set_size(s_faultBox, kLeftW, 24);
  lv_obj_set_pos(s_faultBox, kPad, 170 - kPad - 24);
  lv_obj_set_style_pad_hor(s_faultBox, 8, 0);
  lv_obj_set_style_radius(s_faultBox, 8, 0);
  lv_obj_set_style_bg_opa(s_faultBox, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(s_faultBox, lv_color_hex(kFaultBg), 0);
  lv_obj_set_flex_flow(s_faultBox, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(s_faultBox, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(s_faultBox, 6, 0);
  makeLabel(s_faultBox, &lv_font_montserrat_14, kFaultText, LV_SYMBOL_WARNING);
  makeLabel(s_faultBox, &ui_font_semi_11, kFaultText, "Sensor fault \xC2\xB7 heat is off");
  lv_obj_set_hidden(s_faultBox, true);

  // ---- Right column: +, set point, - (canvas gap 8 -> 4) ----
  makeButton(scr, kPad, true, +1);
  makeButton(scr, 170 - kPad - kBtnH, false, -1);

  // Caption 18 px / 600, letter-spacing 2 -> 9 px SemiBold, spacing 1, muted
  s_setCaption = makeLabel(scr, &ui_font_caption_9, kMuted, "SET");
  lv_obj_set_style_text_letter_space(s_setCaption, 1, 0);
  lv_obj_set_width(s_setCaption, kRightW);
  lv_obj_set_style_text_align(s_setCaption, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(s_setCaption, kRightX, yForBaseline(&ui_font_caption_9, kCaptionBaseline));

  // Set point 52 px / 700 -> 26 px Bold, orange
  s_setValue = makeLabel(scr, &ui_font_set_26, kHeatText, "--\xC2\xB0");
  lv_obj_set_width(s_setValue, kRightW);
  lv_obj_set_style_text_align(s_setValue, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(s_setValue, kRightX, yForBaseline(&ui_font_set_26, kSetBaseline));
}

void homeUpdate(const HomeState& s) {
  char buf[24];

  // Clock
  if (s.timeValid) {
    const int h12 = (s.hour % 12 == 0) ? 12 : s.hour % 12;
    snprintf(buf, sizeof(buf), "%d:%02d %s", h12, s.minute, s.hour < 12 ? "AM" : "PM");
    lv_label_set_text(s_clock, buf);
  } else {
    lv_label_set_text(s_clock, NO_TIME_DISPLAY_TEXT);
  }

  // Temperature, always one decimal place, e.g. "68" + ".4°".
  // With no sensor a fixed placeholder is shown so the screen still reads normally.
  const float shownF = s.sensorOk ? s.tempF : NO_SENSOR_DISPLAY_TEMP_F;
  const long tenths = lroundf(shownF * 10.0f);
  snprintf(buf, sizeof(buf), "%ld", tenths / 10);
  lv_label_set_text(s_whole, buf);
  snprintf(buf, sizeof(buf), ".%ld\xC2\xB0", labs(tenths % 10));
  lv_label_set_text(s_frac, buf);
  lv_obj_update_layout(s_whole);
  // Canvas letter-spacing (-2) also follows the last digit, so the fraction starts 2 px closer.
  lv_obj_set_pos(s_frac, kPad + lv_obj_get_width(s_whole) - 2,
                 yForBaseline(&ui_font_frac_32, kTempBaseline));

  // Bottom row: fault box, or humidity plus the Heating pill
  if (!s.sensorOk) {
    lv_label_set_text(s_humidity, "");
    lv_obj_set_hidden(s_heatPill, true);
    lv_obj_set_hidden(s_faultBox, false);
  } else {
    lv_obj_set_hidden(s_faultBox, true);
    snprintf(buf, sizeof(buf), "%d%% humidity", static_cast<int>(lroundf(s.humidity)));
    lv_label_set_text(s_humidity, buf);
    if (s.heating) {
      lv_obj_update_layout(s_humidity);
      lv_obj_set_pos(s_heatPill, kPad + lv_obj_get_width(s_humidity) + 8, kBottomBaseline - 13);
      lv_obj_set_hidden(s_heatPill, false);
    } else {
      lv_obj_set_hidden(s_heatPill, true);
    }
  }

  // Set point: bold orange, muted while the mode is Off
  lv_obj_set_style_text_color(s_setValue, lv_color_hex(s.modeOff ? kMuted : kHeatText), 0);
  snprintf(buf, sizeof(buf), "%d\xC2\xB0", static_cast<int>(lroundf(s.setpointF)));
  lv_label_set_text(s_setValue, buf);
  lv_label_set_text(s_setCaption, s.modeOff ? "HEAT OFF" : (s.overridden ? "MANUAL" : "SET"));
}

}  // namespace ui
