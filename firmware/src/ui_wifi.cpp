#include "ui_wifi.h"

#include <lvgl.h>
#include <stdio.h>
#include <string.h>

namespace ui {

// Same panel-tuned palette as ui_home.cpp
static const uint32_t kBg = 0x000000;
static const uint32_t kSurface = 0x1A1A1A;
static const uint32_t kSurfacePressed = 0x333333;
static const uint32_t kText = 0xFFFFFF;
static const uint32_t kMuted = 0x9A9A9A;
static const uint32_t kHeatText = 0xFFA050;

static const int kPad = 14;
static const int kHeaderH = 40;
static const int kQrSize = 124;

static lv_obj_t* s_screen;
static lv_obj_t* s_title;
static lv_obj_t* s_text;       // status, shown when the hotspot is closed
static lv_obj_t* s_button;
static lv_obj_t* s_buttonLabel;
static lv_obj_t* s_qr;
static lv_obj_t* s_steps;      // instructions beside the QR code
static char s_qrPassword[16] = "";
static void (*s_onBack)() = nullptr;

static lv_obj_t* makeLabel(lv_obj_t* parent, uint32_t color, const char* text) {
  lv_obj_t* l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  lv_label_set_text(l, text);
  return l;
}

static lv_obj_t* makeButton(int x, int y, int w, int h, const char* text) {
  lv_obj_t* b = lv_button_create(s_screen);
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

static void backEvent(lv_event_t*) {
  net::stopSetup();  // leaving the screen closes the hotspot
  if (s_onBack) s_onBack();
}

static void setupEvent(lv_event_t*) { net::startSetup(); }

void wifiCreate(void (*onBack)()) {
  s_onBack = onBack;

  s_screen = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(s_screen, lv_color_hex(kBg), 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_set_scrollable(s_screen, false);

  lv_obj_t* back = makeButton(0, 0, 96, kHeaderH, LV_SYMBOL_LEFT " Back");
  lv_obj_set_style_bg_opa(back, LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_opa(back, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_add_event_cb(back, backEvent, LV_EVENT_CLICKED, nullptr);
  s_title = makeLabel(s_screen, kMuted, "Wi-Fi");
  lv_obj_align(s_title, LV_ALIGN_TOP_RIGHT, -kPad, 13);

  s_text = makeLabel(s_screen, kText, "");
  lv_obj_set_pos(s_text, kPad, kHeaderH + 6);
  lv_obj_set_width(s_text, 320 - 2 * kPad);

  s_button = makeButton(kPad, 170 - kPad - 46, 320 - 2 * kPad, 46, "");
  s_buttonLabel = lv_obj_get_child(s_button, 0);
  lv_obj_add_event_cb(s_button, setupEvent, LV_EVENT_CLICKED, nullptr);

  // QR code on the right, full height: dark on white, with its quiet zone, so phones can read it.
  s_qr = lv_qrcode_create(s_screen);
  lv_qrcode_set_quiet_zone(s_qr, true);
  lv_qrcode_set_size(s_qr, kQrSize);
  lv_qrcode_set_dark_color(s_qr, lv_color_hex(0x000000));
  lv_qrcode_set_light_color(s_qr, lv_color_hex(0xFFFFFF));
  lv_obj_set_pos(s_qr, 320 - kPad - kQrSize, 170 - 4 - kQrSize);
  lv_obj_set_hidden(s_qr, true);

  s_steps = makeLabel(s_screen, kText, "");
  lv_obj_set_pos(s_steps, kPad, kHeaderH + 2);
  lv_obj_set_width(s_steps, 320 - 3 * kPad - kQrSize);
  lv_obj_set_hidden(s_steps, true);
}

const char* wifiSummary(const net::Status& s) {
  switch (s.state) {
    case net::State::NotSetUp: return "Not set up";
    case net::State::Connecting: return "Connecting";
    case net::State::Connected: return "Connected";
    case net::State::Offline: return "Offline";
    case net::State::Starting:
    case net::State::Hotspot:
    case net::State::Trying: return "Setting up";
  }
  return "";
}

void wifiUpdate(const net::Status& s, const char* serverText) {
  char buf[160];
  const bool hotspot = s.state == net::State::Hotspot;
  const bool busy = s.state == net::State::Starting || s.state == net::State::Trying;
  lv_obj_set_hidden(s_qr, !hotspot);
  lv_obj_set_hidden(s_steps, !hotspot);
  lv_obj_set_hidden(s_text, hotspot);
  lv_obj_set_hidden(s_button, hotspot || busy);

  if (hotspot) {
    if (strcmp(s_qrPassword, s.apPassword) != 0) {  // re-encode only for a new hotspot
      strlcpy(s_qrPassword, s.apPassword, sizeof(s_qrPassword));
      snprintf(buf, sizeof(buf), "WIFI:T:WPA;S:%s;P:%s;;", s.apName, s.apPassword);
      lv_qrcode_update(s_qr, buf, strlen(buf));
    }
    snprintf(buf, sizeof(buf), "Scan, or join:\n%s\n%s\n\nThen open:\n192.168.4.1", s.apName, s.apPassword);
    lv_label_set_text(s_steps, buf);
    return;
  }

  switch (s.state) {
    case net::State::Connected:
      snprintf(buf, sizeof(buf), "Connected to %s\n%s\n%s", s.ssid, s.ip, serverText);
      break;
    case net::State::Connecting:
      snprintf(buf, sizeof(buf), "Connecting to %s...", s.ssid);
      break;
    case net::State::Offline:
      snprintf(buf, sizeof(buf), "Can't reach %s. Retrying.", s.ssid);
      break;
    case net::State::Trying:
      snprintf(buf, sizeof(buf), "Joining %s...", s.ssid);
      break;
    case net::State::Starting:
      snprintf(buf, sizeof(buf), "Starting the hotspot...");
      break;
    default:
      snprintf(buf, sizeof(buf), "Not set up");
      break;
  }
  char full[220];
  snprintf(full, sizeof(full), "%s%s", s.setupFailed ? "Setup didn't work. Check the password.\n" : "", buf);
  lv_label_set_text(s_text, full);
  lv_obj_set_style_text_color(s_text, lv_color_hex(s.setupFailed ? kHeatText : kText), 0);
  lv_label_set_text(s_buttonLabel, s.ssid[0] != '\0' ? "Change network" : "Set up Wi-Fi");
}

void wifiShow() { lv_screen_load(s_screen); }

}  // namespace ui
