# Thermostat firmware

PlatformIO project for the LilyGO T-Display-S3 Touch. Open this `firmware` folder in VS Code with the PlatformIO extension.

## In this version

- Sensor reading every 10 s. Detects a BMP280 (temperature and pressure, no humidity), BME280 or BME688 (temperature, humidity, pressure; gas heater off) at I2C address 0x76 or 0x77. The serial monitor prints which one was found
- Heat-only control with swing and minimum on/off times
- Relay output on `PIN_RELAY`
- Home screen (design option 1, "Big Number") with + / − set point buttons
- Schedule and manual-override logic; the schedule is made on the server's web page (first start has none, and a set point of 68 °F)
- Settings screen (tap the gear, top left): mode Heat/Off, swing, sensor offset, brightness, dim timeout
- Settings and the set point are saved in flash about 2 s after the last change
- When idle the screen dims and shows only the temperature (red on a sensor fault) and time; the first touch only wakes it, to Home
- Watchdog: a main loop hung for 8 s resets the board
- Wi-Fi, set up from Settings > Wi-Fi through a temporary hotspot and web page, with a QR code on the screen
- Optional thermostat name (up to 16 characters), set on the same page; it replaces "Inside" on Home. To change only the name, leave the network on "Keep"
- Clock from network time (US Eastern, with daylight saving)

- 5-minute records (average temperature, humidity and pressure, set point, share of time heating), sent to the server in `../server`, which keeps the history. While it is not answering they are kept in flash (1 MB, about 226 days) and sent, oldest first, when it is back. The server address and token are set on the Wi-Fi setup page; the Wi-Fi screen shows the state

- Status line at the bottom of Home: "Heat on", "Server not reachable", "Server rejected the token", "No Wi-Fi". Several are joined, and the text scrolls if it does not fit; on a sensor fault they follow the fault text. The sync with the server, each 10 s, is the check
- Graph screen: tap the temperature on Home. Temperature (white, with a grey band from lowest to highest) and set point (orange) over 1 day, 5 days or a month, fetched from the server and refreshed each 5 minutes

- Configuration from the server: set point, mode, swing, sensor offset, schedule and name are synced each 10 s, and soon after a change on the screen; the newest change of each wins ([docs/sync.md](../docs/sync.md)). While the server is down the thermostat heats on what it has, kept in flash

Not yet included: Info screen, OTA. The thermostat will have no REST API: it makes every connection itself. Its only HTTP server is the temporary setup page.

## Commands

| Task | Command |
|---|---|
| Build and upload | `pio run -e t-display-s3 -t upload` |
| Serial monitor | `pio device monitor` |
| Logic tests on the PC | `pio test -e native` (needs `g++` on the PATH; the Strawberry Perl one, also inside Lyrion Music Server's `Perl\c\bin`, works) |

## Status of checks

- `lib/thermo_core`: all 32 logic tests (13 control and schedule, 3 settings, 3 Wi-Fi and name text, 7 record, cache and upload, 2 graph, 4 sync) passed on the PC. `.github/workflows/ci.yml` is set to run them on pull requests and on pushes to `develop` and `main`, and to build the firmware and the server.
- Firmware builds and boots on the bare T-Display-S3 board (no sensor or relay) with no resets in 25 s. Set point +, the settings screen (scrolling and buttons), saving settings, Off mode display, the look of the dimmed screen and wake-up touch were checked by hand.
- Wi-Fi setup through the hotspot was checked on the board: a failed first attempt was reported and setup could be repeated, the second attempt connected, and the log showed network time setting the clock. The `request handler not found` log lines during setup are harmless (requests for pages that do not exist, answered by a redirect to the setup page).
- Setting a thermostat name, and reconnecting to Wi-Fi after a reset without setup, were also checked on the board.
- A BMP280 was detected at 0x76 and shows temperatures; no I2C errors in 25 s.
- The relay followed heat calls, minimum times (shortened to 5 s for the test) and what appears to be Mode Off; with the full times, heat came on 180 s after startup. No boiler was connected.
- Records reached the server. With the server stopped, 2 records were kept and then sent after it restarted, with none missing. Some serial lines came out cut short during this.
- The Graph screen was checked by eye with under an hour of records.
- The status line was checked by hand with the server stopped and started.
- Configuration sync was tried by hand on the board with the real server and page, and worked.
- **Not yet confirmed on hardware:** cached records surviving a thermostat restart, recovering from a router outage, keeping the saved network when a new one fails, a watchdog reset, sensor accuracy and fault handling (unplugged, power blip), the BME280 and BME688, the Graph screen with days of real records, the status line's "No Wi-Fi" and token messages, the relay staying off during a reset and with no sensor.

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
- The Wi-Fi password and server token are stored unencrypted in the board's flash, and records are sent over plain HTTP.
- Heating control does not depend on Wi-Fi; the network runs in its own task.
- The clock shows a fixed 12:00 PM placeholder until network time is received (`NO_TIME_DISPLAY_TEXT` in `include/config.h`; set it to `--:--` before real use).

## Layout

```
include/           pins.h, config.h, lv_conf.h
lib/thermo_core/   control, schedule, settings and Wi-Fi text logic (no hardware dependencies)
src/               main, display + touch, sensor, relay, stored settings, Wi-Fi + time, home, settings, Wi-Fi and idle screens
test/test_core/    unit tests for thermo_core
```
