// Minimal LVGL 9 configuration. Anything not set here takes LVGL's default.
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_FORMAT_DEFAULT LV_COLOR_FORMAT_RGB565  // 16-bit colour (replaces LV_COLOR_DEPTH 16)
#define LV_MEM_SIZE (64 * 1024U)

#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_36 1  // idle screen time
#define LV_FONT_DEFAULT &lv_font_montserrat_14

#define LV_USE_QRCODE 1  // Wi-Fi setup screen

#define LV_USE_LOG 0

#endif  // LV_CONF_H
