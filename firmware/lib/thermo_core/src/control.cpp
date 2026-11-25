#include "control.h"

namespace thermo {

float clampSetpoint(float f) {
  if (f < kMinSetpointF) return kMinSetpointF;
  if (f > kMaxSetpointF) return kMaxSetpointF;
  return f;
}

Controller::Controller(const ControlConfig& cfg) { setConfig(cfg); }

void Controller::setConfig(const ControlConfig& cfg) {
  cfg_ = cfg;
  if (cfg_.swingF < kMinSwingF) cfg_.swingF = kMinSwingF;
  if (cfg_.swingF > kMaxSwingF) cfg_.swingF = kMaxSwingF;
}

bool Controller::update(float tempF, bool sensorOk, uint32_t nowS) {
  bool want = heating_;
  bool force = false;

  if (mode_ == Mode::Off || !sensorOk) {
    want = false;
    force = true;  // safety: ignore the minimum on time
  } else {
    const float half = cfg_.swingF * 0.5f;
    if (tempF <= setpointF_ - half) {
      want = true;
    } else if (tempF >= setpointF_ + half) {
      want = false;
    }
    // Inside the band: keep the current state.
  }

  waitS_ = waitTotalS_ = 0;
  if (want != heating_) {
    const uint32_t elapsed = nowS - lastChangeS_;
    const uint32_t minimum = want ? cfg_.minOffS : cfg_.minOnS;
    if (force || elapsed >= minimum) {
      heating_ = want;
      lastChangeS_ = nowS;
    } else {
      waitS_ = minimum - elapsed;
      waitTotalS_ = minimum;
    }
  }
  return heating_;
}

}  // namespace thermo
