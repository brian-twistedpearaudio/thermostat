#include "ui_graph.h"

#include <Arduino.h>
#include <lvgl.h>
#include <stdio.h>
#include <time.h>

#include "fonts/ui_fonts.h"
#include "uplink.h"

namespace ui {

// Same panel-tuned palette as ui_home.cpp
static const uint32_t kBg = 0x000000;
static const uint32_t kSurface = 0x1A1A1A;
static const uint32_t kSurfacePressed = 0x333333;
static const uint32_t kText = 0xFFFFFF;     // temperature line
static const uint32_t kMuted = 0x9A9A9A;
static const uint32_t kSetLine = 0xFFA050;  // set point, as on Home
static const uint32_t kGrid = 0x2E2E2E;
static const uint32_t kBand = 0x4A4A4A;     // lowest to highest within each point

static const int kW = 320;
static const int kHeaderH = 40;
static const int kPlotX = 28, kPlotY = 46;
static const int kPlotW = thermo::kGraphPoints, kPlotH = 104;
static const int kMaxRows = 6, kMaxCols = 8;

struct Range {
  const char* name;
  uint32_t bucket;  // seconds per point
};
static const Range kRanges[] = {{"1 day", 300}, {"5 days", 1500}, {"Month", 9000}};
static const int kRangeCount = sizeof(kRanges) / sizeof(kRanges[0]);

static const uint32_t kRefreshMs = 300000;  // a record is made each 5 minutes
static const uint32_t kRetryMs = 15000;     // after a failure

static lv_obj_t* s_screen;
static lv_obj_t* s_plot;
static lv_obj_t* s_message;
static lv_obj_t* s_buttons[kRangeCount];
static lv_obj_t* s_rowLabels[kMaxRows];
static lv_obj_t* s_colLabels[kMaxCols];
static void (*s_onBack)() = nullptr;

static int s_range = 0;
static bool s_waiting = false;   // a request has not been answered
static bool s_failed = false;    // the last answer was not Ok
static uint32_t s_askedMs = 0;
static uplink::History s_next;   // last answer
static uplink::History s_shown;  // what is drawn
static bool s_hasData = false;
static int s_lo = 0, s_hi = 1, s_step = 10;
static int s_rows[kMaxRows], s_rowCount = 0;  // y of the grid lines, in the plot
static int s_cols[kMaxCols], s_colCount = 0;  // x of the grid lines, in the plot

static lv_obj_t* makeLabel(lv_obj_t* parent, const lv_font_t* font, uint32_t color, const char* text) {
  lv_obj_t* l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  lv_label_set_text(l, text);
  return l;
}

static lv_obj_t* makeButton(int x, int y, int w, int h, const lv_font_t* font, const char* text) {
  lv_obj_t* b = lv_button_create(s_screen);
  lv_obj_remove_style_all(b);
  lv_obj_set_pos(b, x, y);
  lv_obj_set_size(b, w, h);
  lv_obj_set_style_radius(b, 10, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(kSurfacePressed), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_center(makeLabel(b, font, kText, text));
  return b;
}

static int yOf(int tenths) { return (s_hi - tenths) * (kPlotH - 1) / (s_hi - s_lo); }

static void showMessage(const char* text) {
  lv_label_set_text(s_message, text);
  lv_obj_set_hidden(s_message, text[0] == '\0');
}

// ---- Drawing ----

struct Painter {
  lv_layer_t* layer;
  int x0, y0;
  void fill(int x, int y, int w, int h, uint32_t color) const {
    lv_draw_fill_dsc_t d;
    lv_draw_fill_dsc_init(&d);
    d.color = lv_color_hex(color);
    d.opa = LV_OPA_COVER;
    const lv_area_t a = {x0 + x, y0 + y, x0 + x + w - 1, y0 + y + h - 1};
    lv_draw_fill(layer, &d, &a);
  }
  // One line of the graph, a column per point and 2 px thick. A missing point breaks it.
  void line(int16_t thermo::GraphPoint::*field, uint32_t color) const {
    int prev = thermo::kGraphNone;
    for (int i = 0; i < thermo::kGraphPoints; ++i) {
      const int v = s_shown.points[i].*field;
      if (v == thermo::kGraphNone) {
        prev = v;
        continue;
      }
      const int y = yOf(v);
      const int from = prev == thermo::kGraphNone ? y : yOf(prev);
      const int top = from < y ? from : y;
      fill(i, top, prev == thermo::kGraphNone ? 2 : 1, (from < y ? y - from : from - y) + 2, color);
      prev = v;
    }
  }
};

static void drawEvent(lv_event_t* e) {
  if (!s_hasData) return;
  lv_area_t a;
  lv_obj_get_coords(s_plot, &a);
  const Painter p = {lv_event_get_layer(e), static_cast<int>(a.x1), static_cast<int>(a.y1)};

  for (int i = 0; i < s_rowCount; ++i) p.fill(0, s_rows[i], kPlotW, 1, kGrid);
  for (int i = 0; i < s_colCount; ++i) p.fill(s_cols[i], 0, 1, kPlotH, kGrid);
  for (int i = 0; i < thermo::kGraphPoints; ++i) {
    const thermo::GraphPoint& q = s_shown.points[i];
    if (q.min == thermo::kGraphNone || q.max == thermo::kGraphNone || q.min == q.max) continue;
    const int top = yOf(q.max);
    p.fill(i, top, 1, yOf(q.min) - top + 2, kBand);
  }
  p.line(&thermo::GraphPoint::set, kSetLine);
  p.line(&thermo::GraphPoint::avg, kText);
}

// ---- Grid and its labels ----

static void placeRows() {
  s_rowCount = 0;
  int v = s_lo / s_step * s_step;
  if (v < s_lo) v += s_step;
  for (; v <= s_hi && s_rowCount < kMaxRows; v += s_step) {
    const int y = yOf(v);
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", v / 10);
    lv_obj_t* l = s_rowLabels[s_rowCount];
    lv_label_set_text(l, buf);
    lv_obj_set_pos(l, 0, kPlotY + y - lv_font_get_line_height(&ui_font_body_11) / 2);
    s_rows[s_rowCount++] = y;
  }
  for (int i = 0; i < kMaxRows; ++i) lv_obj_set_hidden(s_rowLabels[i], i >= s_rowCount);
}

static void placeCols() {
  s_colCount = 0;
  const thermo::GraphSpan& span = s_shown.span;
  const uint32_t first = (span.start + 3599) / 3600 * 3600;  // the time zone is a whole number of hours
  int days = 0;
  for (uint32_t t = first; t < span.end() && s_colCount < kMaxCols; t += 3600) {
    const time_t tt = t;
    struct tm m;
    localtime_r(&tt, &m);
    char buf[16];
    if (s_range == 0) {
      if (m.tm_hour % 6 != 0) continue;
      snprintf(buf, sizeof(buf), "%d %s", m.tm_hour % 12 == 0 ? 12 : m.tm_hour % 12, m.tm_hour < 12 ? "AM" : "PM");
    } else {
      if (m.tm_hour != 0) continue;
      if (s_range == 2 && days++ % 7 != 0) continue;
      strftime(buf, sizeof(buf), s_range == 1 ? "%a" : "%b %d", &m);
    }
    const int x = static_cast<int>((t - span.start) / span.bucket);
    lv_obj_t* l = s_colLabels[s_colCount];
    lv_label_set_text(l, buf);
    lv_obj_update_layout(l);
    const int w = lv_obj_get_width(l);
    int lx = kPlotX + x - w / 2;
    if (lx > kW - 2 - w) lx = kW - 2 - w;
    lv_obj_set_pos(l, lx, kPlotY + kPlotH + 4);
    s_cols[s_colCount++] = x;
  }
  for (int i = 0; i < kMaxCols; ++i) lv_obj_set_hidden(s_colLabels[i], i >= s_colCount);
}

static void clearData() {
  s_hasData = false;
  for (lv_obj_t* l : s_rowLabels) lv_obj_set_hidden(l, true);
  for (lv_obj_t* l : s_colLabels) lv_obj_set_hidden(l, true);
  lv_obj_invalidate(s_plot);
}

// ---- Asking ----

static void ask() {
  s_waiting = true;
  s_askedMs = millis();
  uplink::requestHistory(kRanges[s_range].bucket);
}

static void answered() {
  s_waiting = false;
  s_failed = s_next.state != uplink::HistoryState::Ok;
  switch (s_next.state) {
    case uplink::HistoryState::Ok: break;
    case uplink::HistoryState::NoServer: showMessage("No server is set.\nSee Settings > Wi-Fi."); return;
    case uplink::HistoryState::NoWifi: showMessage("Wi-Fi is not connected"); return;
    case uplink::HistoryState::NoClock: showMessage("The time is not set yet"); return;
    case uplink::HistoryState::NoAnswer: showMessage("Server not answering"); return;
    case uplink::HistoryState::BadToken: showMessage("Server rejected the token"); return;
    case uplink::HistoryState::NoRecords: showMessage("No records on the server yet"); return;
    case uplink::HistoryState::OldServer: showMessage("The server needs updating"); return;
  }
  s_shown = s_next;
  if (!thermo::graphScale(s_shown.points, s_lo, s_hi, s_step)) {
    clearData();
    showMessage("No records in this time");
    return;
  }
  s_hasData = true;
  showMessage("");
  placeRows();
  placeCols();
  lv_obj_invalidate(s_plot);
}

void graphPoll() {
  if (uplink::takeHistory(s_next)) answered();
  if (s_waiting || lv_screen_active() != s_screen) return;
  if (millis() - s_askedMs >= (s_failed ? kRetryMs : kRefreshMs)) ask();
}

static void selectRange(int range) {
  s_range = range;
  for (int i = 0; i < kRangeCount; ++i) {
    lv_obj_set_style_bg_opa(s_buttons[i], i == range ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(s_buttons[i], 0), lv_color_hex(i == range ? kText : kMuted), 0);
  }
  clearData();
  showMessage("Loading...");
  ask();
}

static void rangeEvent(lv_event_t* e) {
  selectRange(static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e))));
}

static void backEvent(lv_event_t*) {
  if (s_onBack) s_onBack();
}

void graphCreate(void (*onBack)()) {
  s_onBack = onBack;

  s_screen = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(s_screen, lv_color_hex(kBg), 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_set_scrollable(s_screen, false);

  lv_obj_t* back = makeButton(0, 0, 96, kHeaderH, &lv_font_montserrat_14, LV_SYMBOL_LEFT " Back");
  lv_obj_add_event_cb(back, backEvent, LV_EVENT_CLICKED, nullptr);
  const int bw = 68;
  for (int i = 0; i < kRangeCount; ++i) {
    s_buttons[i] = makeButton(kW - 4 - (kRangeCount - i) * (bw + 4) + 4, 2, bw, kHeaderH - 4,
                              &lv_font_montserrat_14, kRanges[i].name);
    lv_obj_set_style_bg_color(s_buttons[i], lv_color_hex(kSurface), 0);
    lv_obj_add_event_cb(s_buttons[i], rangeEvent, LV_EVENT_CLICKED,
                        reinterpret_cast<void*>(static_cast<intptr_t>(i)));
  }

  s_plot = lv_obj_create(s_screen);
  lv_obj_remove_style_all(s_plot);
  lv_obj_set_pos(s_plot, kPlotX, kPlotY);
  lv_obj_set_size(s_plot, kPlotW, kPlotH + 2);  // a line at the bottom is 2 px thick
  lv_obj_set_clickable(s_plot, false);
  lv_obj_set_scrollable(s_plot, false);
  lv_obj_add_event_cb(s_plot, drawEvent, LV_EVENT_DRAW_MAIN, nullptr);

  for (lv_obj_t*& l : s_rowLabels) {
    l = makeLabel(s_screen, &ui_font_body_11, kMuted, "");
    lv_obj_set_width(l, kPlotX - 5);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_RIGHT, 0);
  }
  for (lv_obj_t*& l : s_colLabels) l = makeLabel(s_screen, &ui_font_body_11, kMuted, "");

  s_message = makeLabel(s_screen, &lv_font_montserrat_14, kText, "");
  lv_obj_set_style_text_align(s_message, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_bg_color(s_message, lv_color_hex(kBg), 0);
  lv_obj_set_style_bg_opa(s_message, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(s_message, 6, 0);
  lv_obj_align(s_message, LV_ALIGN_TOP_MID, 0, kPlotY + 30);
  clearData();
}

void graphShow() {
  selectRange(s_range);
  lv_screen_load(s_screen);
}

}  // namespace ui
