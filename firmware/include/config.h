// Compile-time constants. User-editable settings and their defaults are in
// lib/thermo_core/src/settings.h.
#pragma once

#define SETPOINT_STEP_F 1.0f      // change per press of + / -
#define MIN_ON_SECONDS 120
#define MIN_OFF_SECONDS 180
#define WATCHDOG_SECONDS 8        // main loop hung this long: reset, which releases the relay

#define SENSOR_PERIOD_MS 10000   // BME688 read interval
#define NO_SENSOR_DISPLAY_TEMP_F 69.4f  // shown as the inside temperature while no sensor is found
// TEMPORARY, for checking the screen layout: shown until the clock is set. Display only; the
// schedule never sees it. Use "--:--" to go back to showing that the time is unknown.
#define NO_TIME_DISPLAY_TEXT "12:00 PM"
#define TEMP_SMOOTHING 0.3f      // 0..1, weight of each new reading

// TEMPORARY screen demo: 1 = pretend a sensor is present and alternate the home screen
// between "heating" and "not heating" every UI_DEMO_PERIOD_MS. Display only: the control
// logic and relay still use the real sensor state. Set to 0 to go back to normal.
#define UI_DEMO_CYCLE 0
#define UI_DEMO_PERIOD_MS 5000

// 1 or 3: landscape, depending on which side the USB connector should face.
#define DISPLAY_ROTATION 1

// US Eastern, with daylight saving.
#define TIMEZONE "EST5EDT,M3.2.0,M11.1.0"
