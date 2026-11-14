# Thermostat firmware

PlatformIO project for the LilyGO T-Display-S3 Touch. Open this `firmware` folder in VS Code with the PlatformIO extension.

## Stage 1 (this version)

- BME688 reading every 10 s (temperature, humidity, pressure; gas heater off)
- Heat-only control with swing and minimum on/off times
- Relay output on `PIN_RELAY`
- Home screen (design option 1, "Big Number") with + / − set point buttons
- Schedule and manual-override logic (no schedule loaded yet, so the set point starts at 68 °F and changes only by hand)

Not yet included: stored settings, 5-minute logging, Graph / Settings / Info screens, Wi-Fi and clock, REST API, server uplink, OTA.

## Commands

| Task | Command |
|---|---|
| Build and upload | `pio run -e t-display-s3 -t upload` |
| Serial monitor | `pio device monitor` |
| Logic tests on the PC | `pio test -e native` (needs a C++ compiler on the PC) |

## Status of checks

- `lib/thermo_core` (control and schedule logic): 13 tests pass.
- Everything under `src/`: written but **not yet compiled or run on hardware**.

## Check on first power-up

1. **Colours:** if red and blue are swapped, change `TFT_RGB_ORDER` to `TFT_BGR` in `platformio.ini`.
2. **Orientation:** `DISPLAY_ROTATION` in `include/config.h` is 1; use 3 to flip the screen 180°.
3. **Touch position:** if presses land mirrored, swap the two mappings marked `VERIFY ON HARDWARE` in `src/display.cpp`.
4. **Relay pin:** `PIN_RELAY` (GPIO 10) in `include/pins.h` is a placeholder until the PCB is laid out. Do not connect a 5 V relay coil directly to the pin; it needs the transistor driver.
5. **Temperature offset:** set `TEMP_OFFSET_F` in `include/config.h` once the sensor is in the enclosure and compared against a reference thermometer.

## Behaviour to know

- After power-up or reset the boiler is not called for the first 3 minutes (minimum off time).
- A sensor fault turns heat off immediately and shows on the screen.
- The clock shows `--:--` until Wi-Fi and network time are added.

## Layout

```
include/           pins.h, config.h, lv_conf.h
lib/thermo_core/   control and schedule logic (no hardware dependencies)
src/               main, display + touch, sensor, relay, home screen
test/test_core/    unit tests for thermo_core
```
