# Thermostat firmware

PlatformIO project for the LilyGO T-Display-S3 Touch. Open this `firmware` folder in VS Code with the PlatformIO extension.

## In this version

- BME688 reading every 10 s (temperature, humidity, pressure; gas heater off)
- Heat-only control with swing and minimum on/off times
- Relay output on `PIN_RELAY`
- Home screen (design option 1, "Big Number") with + / − set point buttons
- Schedule and manual-override logic (no schedule loaded yet, so the set point changes only by hand; first start is 68 °F)
- Settings screen (tap the gear, top left): mode Heat/Off, swing, sensor offset, brightness, dim timeout
- Settings and the set point are saved in flash about 2 s after the last change
- When idle the screen dims and shows only the temperature (red on a sensor fault) and time; the first touch only wakes it, to Home
- Watchdog: a main loop hung for 8 s resets the board

Not yet included: 5-minute logging, Graph / Info screens, Wi-Fi and clock, REST API, server uplink, OTA.

## Commands

| Task | Command |
|---|---|
| Build and upload | `pio run -e t-display-s3 -t upload` |
| Serial monitor | `pio device monitor` |
| Logic tests on the PC | `pio test -e native` (needs a C++ compiler on the PC) |

## Status of checks

- `lib/thermo_core` (control and schedule logic): 13 control and schedule tests passed earlier. 3 settings tests added since; they compile, but have **not been run** (no PC compiler yet).
- Firmware builds and boots on the bare T-Display-S3 board (no sensor or relay) with no resets in 25 s. Set point +, the settings screen (scrolling and buttons), saving settings, Off mode display, the look of the dimmed screen and wake-up touch were checked by hand.
- **Not yet confirmed on hardware:** a watchdog reset, sensor readings, heating (including Off mode stopping a heat call) and the relay.

## Check on first power-up

1. **Colours:** if red and blue are swapped, change `TFT_RGB_ORDER` to `TFT_BGR` in `platformio.ini`.
2. **Orientation:** `DISPLAY_ROTATION` in `include/config.h` is 1; use 3 to flip the screen 180°.
3. **Touch position:** if presses land mirrored, swap the two mappings marked `VERIFY ON HARDWARE` in `src/display.cpp`.
4. **Relay pin:** `PIN_RELAY` (GPIO 10) in `include/pins.h` is a placeholder until the PCB is laid out. Do not connect a 5 V relay coil directly to the pin; it needs the transistor driver.
5. **Temperature offset:** set Sensor offset on the settings screen once the sensor is in the enclosure and compared against a reference thermometer.
6. **Backlight:** check that Brightness changes the screen and that it dims. `src/display.cpp` assumes an AW9364 backlight driver.
7. **Relay pull-down:** the relay pin floats during reset, so the PCB driver needs a pull-down to keep the relay off.

## Behaviour to know

- After power-up or reset the boiler is not called for the first 3 minutes (minimum off time).
- With no sensor the screen shows a fixed 69.4° placeholder and the fault box, and heat stays off.
- A sensor fault turns heat off immediately and shows on the screen.
- The clock shows a fixed 12:00 PM placeholder until Wi-Fi and network time are added (`NO_TIME_DISPLAY_TEXT` in `include/config.h`; set it to `--:--` before real use).

## Layout

```
include/           pins.h, config.h, lv_conf.h
lib/thermo_core/   control, schedule and settings logic (no hardware dependencies)
src/               main, display + touch, sensor, relay, stored settings, home, settings and idle screens
test/test_core/    unit tests for thermo_core
```
