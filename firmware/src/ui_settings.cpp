// Settings screen. Header with Back, then a scrolling list of rows with large - / + buttons.
#include "ui_settings.h"

#include <lvgl.h>

namespace ui {

// Same panel-tuned palette as ui_home.cpp
static const uint32_t kBg = 0x000000;
static const uint32_t kSurface = 0x1A1A1A;
static const uint32_t kSurfacePressed = 0x333333;
static const uint32_t kText = 0xFFFFFF;
static const uint32_t kMuted = 0x9A9A9A;
static const uint32_t kHeatText = 0xFFA050;

static const int kW = 320, kH = 170;
static const int kPad = 14;
static const int kHeaderH = 40;
static const int kRowH = 54;
static const int kBtnW = 56, kBtnH = 46, kBtnGap = 8;

struct Row {
  thermo::SettingId id;
  const char* name;
  lv_obj_t* value;
};
static Row s_rows[] = {
    {thermo::SettingId::Mode, "Mode", nullptr},
    {thermo::SettingId::Swing, "Swing", nullptr},
    {thermo::SettingId::Offset, "Sensor offset", nullptr},
    {thermo::SettingId::Brightness, "Brightness", nullptr},
    {thermo::SettingId::DimTimeout, "Dim after", nullptr},
};
static const int kRowCount = sizeof(s_rows) / sizeof(s_rows[0]);

static lv_obj_t* s_screen;
static lv_obj_t* s_list;
static lv_obj_t* s_modeLabel;
static void (*s_onStep)(thermo::SettingId, int) = nullptr;
static void (*s_onBack)() = nullptr;

static lv_obj_t* makeLabel(lv_obj_t* parent, uint32_t color, const char* text) {
  lv_obj_t* l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  lv_label_set_text(l, text);
  return l;
}

static lv_obj_t* makeButton(lv_obj_t* parent, int x, int y, int w, int h, const char* text) {
  lv_obj_t* b = lv_button_create(parent);
  lv_obj_remove_style_all(b);
  lv_obj_set_pos(b, x, y);
  lv_obj_set_size(b, w, h);
  lv_obj_set_style_radius(b, 10, 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(kSurface), 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(kSurfacePressed), LV_STATE_PRESSED);
  lv_obj_center(makeLabel(b, kText, text));
  return b;
}

// Event data packs the row and direction into one number: row * 2 + (1 if up).
static void stepEvent(lv_event_t* e) {
  const int v = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  if (s_onStep) s_onStep(s_rows[v / 2].id, (v & 1) ? +1 : -1);
}

static void backEvent(lv_event_t*) {
  if (s_onBack) s_onBack();
}

static void addStepButton(lv_obj_t* row, int x, int index, bool up) {
  lv_obj_t* b = makeButton(row, x, (kRowH - kBtnH) / 2, kBtnW, kBtnH, up ? LV_SYMBOL_PLUS : LV_SYMBOL_MINUS);
  void* ud = reinterpret_cast<void*>(static_cast<intptr_t>(index * 2 + (up ? 1 : 0)));
  lv_obj_add_event_cb(b, stepEvent, LV_EVENT_SHORT_CLICKED, ud);
  lv_obj_add_event_cb(b, stepEvent, LV_EVENT_LONG_PRESSED_REPEAT, ud);  // hold to keep stepping
}

void settingsCreate(void (*onStep)(thermo::SettingId, int), void (*onBack)()) {
  s_onStep = onStep;
  s_onBack = onBack;

  s_screen = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(s_screen, lv_color_hex(kBg), 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_set_scrollable(s_screen, false);

  lv_obj_t* back = makeButton(s_screen, 0, 0, 96, kHeaderH, LV_SYMBOL_LEFT " Back");
  lv_obj_set_style_bg_opa(back, LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_opa(back, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_add_event_cb(back, backEvent, LV_EVENT_CLICKED, nullptr);
  lv_obj_align(makeLabel(s_screen, kMuted, "Settings"), LV_ALIGN_TOP_RIGHT, -kPad, 13);

  s_list = lv_obj_create(s_screen);
  lv_obj_remove_style_all(s_list);
  lv_obj_set_pos(s_list, 0, kHeaderH);
  lv_obj_set_size(s_list, kW, kH - kHeaderH);
  lv_obj_set_scroll_dir(s_list, LV_DIR_VER);

  const int plusX = kW - kPad - kBtnW;
  const int minusX = plusX - kBtnGap - kBtnW;
  for (int i = 0; i < kRowCount; ++i) {
    lv_obj_t* row = lv_obj_create(s_list);
    lv_obj_remove_style_all(row);
    lv_obj_set_pos(row, 0, i * kRowH);
    lv_obj_set_size(row, kW, kRowH);
    lv_obj_set_scrollable(row, false);

    lv_obj_set_pos(makeLabel(row, kMuted, s_rows[i].name), kPad, 8);
    s_rows[i].value = makeLabel(row, kHeatText, "");
    lv_obj_set_pos(s_rows[i].value, kPad, 28);

    if (s_rows[i].id == thermo::SettingId::Mode) {
      // One button, click only: holding it must not flip the mode back and forth.
      lv_obj_t* b = makeButton(row, minusX, (kRowH - kBtnH) / 2, kBtnW * 2 + kBtnGap, kBtnH, "");
      s_modeLabel = lv_obj_get_child(b, 0);
      lv_obj_add_event_cb(b, stepEvent, LV_EVENT_CLICKED,
                          reinterpret_cast<void*>(static_cast<intptr_t>(i * 2 + 1)));
    } else {
      addStepButton(row, minusX, i, false);
      addStepButton(row, plusX, i, true);
    }
  }
}

void settingsUpdate(const thermo::Settings& s) {
  char buf[16];
  for (int i = 0; i < kRowCount; ++i) {
    thermo::formatSetting(s, s_rows[i].id, buf, sizeof(buf));
    lv_label_set_text(s_rows[i].value, buf);
  }
  lv_label_set_text(s_modeLabel, s.heatMode ? "Turn off" : "Turn on");
}

void settingsShow() {
  lv_obj_scroll_to_y(s_list, 0, LV_ANIM_OFF);
  lv_screen_load(s_screen);
}

}  // namespace ui
