// Compile-time defaults. These become stored, editable settings in a later stage.
#pragma once

#define DEFAULT_SETPOINT_F 68.0f  // used until a schedule is supplied
#define SETPOINT_STEP_F 1.0f      // change per press of + / -
#define DEFAULT_SWING_F 1.0f      // total band, centred on the set point
#define MIN_ON_SECONDS 120
#define MIN_OFF_SECONDS 180

#define SENSOR_PERIOD_MS 10000   // BME688 read interval
#define TEMP_OFFSET_F 0.0f       // added to the reading; calibrate in the enclosure
#define NO_SENSOR_DISPLAY_TEMP_F 69.4f  // shown as the inside temperature while no sensor is found
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
