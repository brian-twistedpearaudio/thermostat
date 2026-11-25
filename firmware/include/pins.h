// Pin assignments — LilyGO T-Display-S3 Touch.
#pragma once

// On-board
#define PIN_LCD_POWER 15  // must be HIGH for the LCD to be powered
#define PIN_I2C_SDA 18    // shared by the CST816 touch controller and the sensor
#define PIN_I2C_SCL 17
#define PIN_TOUCH_INT 16
#define PIN_TOUCH_RST 21
#define PIN_BUTTON_1 0
#define PIN_BUTTON_2 14

// Carrier board (TENTATIVE — confirm when the PCB is laid out)
#define PIN_RELAY 10  // HIGH = relay energised = call for heat (via transistor driver)
