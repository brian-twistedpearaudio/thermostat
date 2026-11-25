// Heat-only thermostat control logic. No hardware calls: testable on the PC.
#pragma once
#include <stdint.h>

namespace thermo {

constexpr float kMinSetpointF = 45.0f;
constexpr float kMaxSetpointF = 85.0f;
constexpr float kMinSwingF = 0.5f;
constexpr float kMaxSwingF = 3.0f;

float clampSetpoint(float f);

enum class Mode : uint8_t { Off, Heat };

struct ControlConfig {
  float swingF = 1.0f;      // total band centred on the set point
  uint32_t minOnS = 120;    // once on, stay on at least this long
  uint32_t minOffS = 180;   // once off (or after boot), stay off at least this long
};

class Controller {
 public:
  explicit Controller(const ControlConfig& cfg = ControlConfig());

  void setConfig(const ControlConfig& cfg);
  const ControlConfig& config() const { return cfg_; }
  void setMode(Mode m) { mode_ = m; }
  Mode mode() const { return mode_; }
  void setSetpoint(float f) { setpointF_ = clampSetpoint(f); }
  float setpoint() const { return setpointF_; }

  // Call regularly. nowS is a monotonic clock in seconds, starting near 0 at boot.
  // Heat turns on at setpoint - swing/2 and off at setpoint + swing/2.
  // Mode Off or sensorOk == false turns heat off immediately.
  // Returns true when the boiler should be called.
  bool update(float tempF, bool sensorOk, uint32_t nowS);

  bool heating() const { return heating_; }

  // While a change is held back by a minimum time: seconds until it, and that whole time. Else 0.
  uint32_t waitS() const { return waitS_; }
  uint32_t waitTotalS() const { return waitTotalS_; }

 private:
  ControlConfig cfg_;
  Mode mode_ = Mode::Heat;
  float setpointF_ = 68.0f;
  bool heating_ = false;
  uint32_t waitS_ = 0, waitTotalS_ = 0;
  uint32_t lastChangeS_ = 0;  // boot counts as "just turned off"
};

}  // namespace thermo
