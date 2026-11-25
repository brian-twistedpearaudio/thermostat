# Thermostat — Software Plan (draft for review)

Status: draft 2, 2026-11-14. Nothing here is built yet. Items marked **(proposed)** are my defaults and change with your answers to the open questions at the end.

## Decisions so far

| Topic | Decision |
|---|---|
| Board | T-Display-S3 **Touch** |
| Orientation | Landscape (320×170) |
| Units | °F |
| Swing | Centred band, default 1.0 °F (on at set − 0.5, off at set + 0.5), adjustable 0.5–3.0 °F in 0.5 steps |
| Server | C# / ASP.NET Core, SQLite, runs on Windows or Linux |
| Relay | Omron G6K-2P-DC5 (single-side stable, 5 V coil): one GPIO, on = energised |
| BME688 gas sensor | Not used for now; temperature, humidity, pressure only. Driver kept so gas can be added later |
| Schedules | One schedule of time-of-day set points, the same every day, stored and run on the thermostat; defaults to be supplied |
| Logging | (2026-11-25) The thermostat sends each 5-minute record to the server and keeps no history of its own. While the server is not responding, records are cached on the thermostat and uploaded when it is available again. The server runs on the development Windows PC for now. History is requested from the server per device. Replaces the on-device ring buffer and `/history` below, and answers questions 6 and 10: the API and database have a device ID from the start |
| No server on the thermostat | (2026-11-25) Thermostats run no HTTP server and always pull their configuration (set point, mode, settings, schedule) from the server. They make every connection; nothing connects to them. Replaces the thermostat REST API, `api` module, mDNS name and control forwarding below, and answers question 9 (no page on the thermostat). The thermostat keeps heating on the last configuration it pulled when the server is down. One exception: the temporary hotspot page, for setup only (Wi-Fi, server address and token). It runs only while the hotspot is open and is never reachable on the home network |
| Manual changes | Last until the next schedule event, then the schedule resumes. No Hold |

## 1. Scope

Three pieces of software:

1. **Thermostat firmware** (PlatformIO / VS Code, ESP32-S3) — touch UI, sensor, boiler control, 5-minute logging, pulling its configuration from the server.
2. **Local server** — receives and stores the log long-term, exposes a REST API to read history and control the thermostat, serves a web dashboard.
3. **Shared API contract** — one written spec both sides are built against.

The thermostat must keep heating correctly with Wi-Fi and the server down. The server is an add-on, never a dependency for control.

## 2. Hardware facts the software depends on

| Item | What it means for software |
|---|---|
| LilyGO T-Display-S3, 170×320, 1.9" | Very small screen: one job per screen, large targets, few controls. |
| Touch | Only the **T-Display-S3 Touch** variant has a touch panel (CST816 on I2C). The basic board has no touch — see open question 1. |
| BME688 | I2C, can share the bus with the touch controller. Gas heater and nearby electronics warm the sensor, so firmware needs a calibration offset and a decision on whether the gas sensor runs at all. |
| Omron G6K 5 V relay | Coil needs more current than a GPIO supplies and runs at 5 V, so the PCB needs a transistor driver and flyback diode; firmware sees one GPIO. Relay must be off at boot, on reset and on any fault. A latching G6K variant would need pulse drive instead. |
| 16 MB flash, 8 MB PSRAM | Plenty of room for on-device history, UI assets and OTA updates. |

## 3. Firmware architecture

Framework: Arduino on ESP32-S3 under PlatformIO **(proposed)**, with FreeRTOS tasks. UI library: LVGL **(proposed)** — it has touch widgets and a chart widget, and renders through TFT_eSPI or LovyanGFX.

| Module | Responsibility |
|---|---|
| `sensor` | Read BME688 every ~10 s, apply offset, smooth, flag faults. |
| `control` | Thermostat logic: set point, swing, minimum on/off times, fail-safe. Pure logic with no hardware calls, so it can be unit-tested on the PC. |
| `relay` | Drives the G6K GPIO; counts on-time per logging interval. |
| `schedule` | Time-of-day set points and manual-override handling. Pure logic, unit-tested on the PC. |
| `datalog` | Every 5 minutes (aligned to the clock) store a record in flash; serve ranges for graphs and the API. |
| `settings` | Persistent settings in NVS: set point, swing, units, offset, brightness, server address, API token. |
| `net` | Wi-Fi with auto-reconnect, NTP time (America/New_York), mDNS name such as `thermostat.local`. |
| `api` | REST server on the thermostat (section 6). |
| `uplink` | Push each log record to the local server; queue and backfill when the server is unreachable. |
| `ui` | LVGL screens and touch handling. |
| `ota` | Firmware update over Wi-Fi. |

### Control logic (heat only, proposed)

- Swing is a user setting: a band centred on the set point. Heat turns **on** at set point − swing/2 and **off** at set point + swing/2. Default 1.0 °F, range 0.5–3.0 °F in 0.5 steps. A boiler with radiators or baseboard keeps giving heat after the call ends, so a tighter band mostly adds short cycles without adding comfort.
- Schedule: a list of time-of-day set points held on the thermostat, so it runs without the server. One schedule applies to every day. A manual change on the screen or by API lasts until the next schedule event, then the schedule resumes.
- Short-cycle protection: minimum on time and minimum off time (proposed 2 min / 3 min, configurable).
- Modes: Heat and Off. Optional freeze protection in Off (heat below ~45 °F).
- Fail-safe: sensor fault or implausible reading → relay off, fault shown on screen and in the API. Watchdog resets a hung controller with the relay off.
- Set point changes from touch, REST and server all go through the same function, so the screen always shows the true value.

### Data logging

- Record every 5 minutes: timestamp, temperature, humidity, set point, percent of the interval the boiler was called, flags. Pressure (and gas, if used) optional.
- 288 records/day at ~12 bytes each ≈ 3.5 KB/day. A ring buffer on LittleFS holding 90 days ≈ 310 KB **(proposed)**.
- Graph data is reduced on the device to one point per screen column: 1 day = all 288 points, 5 days = averaged in ~25-minute buckets, 1 month = ~2.5-hour buckets drawn as a min–max band with the average.

## 4. User interface

Home screen: pick one of the 10 options on the design canvas. Whichever is chosen, the full screen set is:

1. **Home** — current temperature, set point with up/down, humidity, heating indicator.
2. **Graph** (optional view) — temperature and set point over 1 day / 5 days / 1 month.
3. **Settings** — swing, mode (Heat/Off), units, sensor offset, brightness and dim timeout.
4. **Info** — Wi-Fi status, IP address, server link status, firmware version.
5. **Fault overlay** — sensor or relay fault.

Behaviour: screen dims after a timeout and wakes on touch; the first touch on a dimmed screen only wakes it. The two side buttons can act as up/down so the thermostat is usable if touch fails.

## 5. Local server

- C# on ASP.NET Core (current LTS release of .NET), SQLite through EF Core or Microsoft.Data.Sqlite. One codebase for Windows and Linux; installable as a Windows service or a systemd service.
- Dashboard also edits the schedule, which the thermostat pulls.
- Stores every 5-minute record indefinitely.
- Web dashboard: current state, set point control, history chart with arbitrary date range, CSV export.
- Holds each thermostat's configuration, which the thermostat pulls; the server never connects to a thermostat. Still to design: how a change made on the thermostat's screen gets back to the server, and how often it pulls.

## 6. REST API (draft)

**Superseded:** the thermostat has no API (see Decisions). The table below and the forwarded control calls are kept only as a list of what the configuration and status must cover; `server/README.md` has the API as built.

Thermostat (`http://thermostat.local/api/v1`):

| Method | Path | Purpose |
|---|---|---|
| GET | `/status` | Temperature, humidity, set point, mode, heating on/off, faults, uptime |
| PUT | `/setpoint` | Change set point |
| PUT | `/mode` | Heat / Off |
| GET, PUT | `/config` | Swing, min on/off times, sensor offset, brightness, server URL |
| GET, PUT | `/schedule` | Time-of-day set points |
| GET | `/history?from=&to=` | Logged records from the on-device buffer |
| GET | `/info` | Firmware version, Wi-Fi signal, free storage |

Server (`/api/v1`):

| Method | Path | Purpose |
|---|---|---|
| POST | `/readings` | Thermostat pushes records (batch, idempotent by timestamp) |
| GET | `/readings?from=&to=&bucket=` | History, optionally averaged |
| GET | `/thermostat/status` | Latest known state |
| PUT | `/thermostat/setpoint`, `/thermostat/mode`, `/thermostat/config`, `/thermostat/schedule` | Control, forwarded to the device |

JSON throughout. Proposed security: a shared API token in a request header, LAN only, plain HTTP.

## 7. Project folder layout (proposed)

```
Thermostat Project/
  docs/            plan, API spec, datasheets
  firmware/        PlatformIO project (src/, include/, lib/, test/, platformio.ini)
  server/          ASP.NET Core solution, dashboard
  pcb/             Fusion Electronics exports, BOM
  enclosure/       Fusion exports, STLs
```

## 8. Build order

1. Bring-up: display, touch, BME688 readout, relay GPIO on the bench.
2. Control logic with PC-side unit tests (swing, min times, fail-safe).
3. Home + Settings screens in the chosen style.
4. Logging to flash and the Graph screen.
5. Wi-Fi, time, pulling configuration from the server.
6. Server: ingest, storage, configuration, dashboard.
7. Uplink with backfill, OTA, fault handling, soak test on the real boiler.

## 9. Open questions

Answered items are in "Decisions so far" at the top. Still open:

1. **Home screen:** which landscape option, 1–7?
2. **Schedule editing:** up to 6 periods per day, edited on the web dashboard only (proposed), or on the thermostat screen too?
4. **Boiler limits:** any minimum on/off time your boiler needs? Proposed 2 min on / 3 min off.
5. **Sensor failure / Off mode:** on a sensor fault, heat off (proposed) or hold last state? Freeze protection when Off?
6. **On-device retention:** is 90 days on the thermostat enough given the server keeps everything? Should pressure be logged too?
7. **Wi-Fi setup:** first-time setup through a temporary setup hotspot and web page (proposed), or credentials compiled in?
8. **Security:** is a shared token over plain HTTP on your LAN enough?
9. **Web page on the thermostat itself:** wanted, or is the server dashboard the only web UI?
10. **More than one thermostat/zone later?** If so the API and database get a device ID from the start.
