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
- Wi-Fi, set up from Settings > Wi-Fi through a temporary hotspot and web page, with a QR code on the screen
- Optional thermostat name (up to 16 characters), set on the same page; it replaces "Inside" on Home. To change only the name, leave the network on "Keep"
- Clock from network time (US Eastern, with daylight saving)

Not yet included: 5-minute logging, Graph / Info screens, REST API, server uplink, OTA.

## Commands

| Task | Command |
|---|---|
| Build and upload | `pio run -e t-display-s3 -t upload` |
| Serial monitor | `pio device monitor` |
| Logic tests on the PC | `pio test -e native` (needs a C++ compiler on the PC) |

## Status of checks

- `lib/thermo_core` (control and schedule logic): 13 control and schedule tests passed earlier. 3 settings and 3 Wi-Fi and name text tests added since; they compile, but have **not been run** (no PC compiler yet).
- Firmware builds and boots on the bare T-Display-S3 board (no sensor or relay) with no resets in 25 s. Set point +, the settings screen (scrolling and buttons), saving settings, Off mode display, the look of the dimmed screen and wake-up touch were checked by hand.
- Wi-Fi setup through the hotspot was checked on the board: a failed first attempt was reported and setup could be repeated, the second attempt connected, and the log showed network time setting the clock. The `request handler not found` log lines during setup are harmless (requests for pages that do not exist, answered by a redirect to the setup page).
- Setting a thermostat name, and reconnecting to Wi-Fi after a reset without setup, were also checked on the board.
- **Not yet confirmed on hardware:** recovering from a router outage, keeping the saved network when a new one fails, a watchdog reset, sensor readings, heating (including Off mode stopping a heat call) and the relay.

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
- Wi-Fi is 2.4 GHz only. The setup hotspot uses a new password each time, shown only on the screen, and closes on Back, after 10 minutes, or once a network is entered. A new network replaces the saved one only after it connects.
- The Wi-Fi password is stored unencrypted in the board's flash.
- Heating control does not depend on Wi-Fi; the network runs in its own task.
- The clock shows a fixed 12:00 PM placeholder until network time is received (`NO_TIME_DISPLAY_TEXT` in `include/config.h`; set it to `--:--` before real use).

## Layout

```
include/           pins.h, config.h, lv_conf.h
lib/thermo_core/   control, schedule, settings and Wi-Fi text logic (no hardware dependencies)
src/               main, display + touch, sensor, relay, stored settings, Wi-Fi + time, home, settings, Wi-Fi and idle screens
test/test_core/    unit tests for thermo_core
```
