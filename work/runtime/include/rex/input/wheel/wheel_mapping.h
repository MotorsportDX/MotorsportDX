/**
 * @file        input/wheel/wheel_mapping.h
 * @brief       The racing wheel's axes and buttons as driving controls
 *
 * Header only and free of runtime and SDL dependencies, so the CPU tests
 * (work/fm4-rex/tests/wheel-mapping-tests.cpp) check the same mapping the
 * wheel host runs. The host maps the device's raw axes (SDL's -32768..32767)
 * and buttons to floats at device resolution, and a title integration writes
 * them into the title's own driving controls (FM4: work/fm4-rex/src/
 * fm4-wheel-input.cpp). See docs/analysis/racing-wheels.md, stage 2.
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace rex::input::wheel {

// A pedal (or any unipolar axis): the raw value at rest and fully pressed (either order, so a
// pedal whose raw value falls as it is pressed needs no inversion), a deadzone at the rest end
// and a response curve. The defaults are an axis that rests at -32768.
struct PedalMap {
  int axis = -1;  // -1: none
  double released = -32768.0;
  double pressed = 32767.0;
  // The fraction of the travel from rest that still reads 0; the rest is stretched to 0..1.
  double deadzone = 0.0;
  // The output is the travel to this power: above 1 is finer at the top of the travel.
  double gamma = 1.0;
};

struct SteeringMap {
  int axis = 0;
  bool invert = false;
  // The rotation the device reports over its whole axis, lock to lock (Pit House's maximum
  // angle on a MOZA base).
  double range_deg = 900.0;
  // The rotation that gives the title's full steering, lock to lock; 0: the device's range.
  double lock_deg = 0.0;
};

struct MappingConfig {
  SteeringMap steering;
  PedalMap throttle{1};
  PedalMap brake{2};
  PedalMap clutch;
  // Button numbers, -1: none.
  int shift_up_button = -1;
  int shift_down_button = -1;
  int clutch_button = -1;
  int handbrake_button = -1;
};

// The driving controls of one device state, at device resolution.
struct Controls {
  bool connected = false;
  double steering = 0.0;  // -1..1 of the lock (right positive)
  double throttle = 0.0;  // 0..1
  double brake = 0.0;     // 0..1
  double clutch = 0.0;    // 0..1, 1 fully pressed
  double handbrake = 0.0; // 0..1
  bool shift_up = false;  // held
  bool shift_down = false;
  // Presses counted since the host started, so a press shorter than a guest frame still
  // shifts once.
  uint32_t shift_up_presses = 0;
  uint32_t shift_down_presses = 0;
};

// -1..1 over the device's whole axis.
inline double NormalizedAxis(int16_t raw) {
  return raw >= 0 ? double(raw) / 32767.0 : double(raw) / 32768.0;
}

inline double MapSteering(const SteeringMap& map, int16_t raw) {
  double value = NormalizedAxis(raw);
  if (map.invert) {
    value = -value;
  }
  const double range = std::isfinite(map.range_deg) && map.range_deg > 0.0 ? map.range_deg : 900.0;
  const double lock =
      std::isfinite(map.lock_deg) && map.lock_deg > 0.0 ? std::min(map.lock_deg, range) : range;
  return std::clamp(value * range / lock, -1.0, 1.0);
}

inline double MapPedal(const PedalMap& map, int16_t raw) {
  const double span = map.pressed - map.released;
  if (!std::isfinite(span) || std::fabs(span) < 1.0) {
    return 0.0;
  }
  double travel = std::clamp((double(raw) - map.released) / span, 0.0, 1.0);
  const double deadzone = std::isfinite(map.deadzone) ? std::clamp(map.deadzone, 0.0, 0.9) : 0.0;
  travel = travel <= deadzone ? 0.0 : (travel - deadzone) / (1.0 - deadzone);
  const double gamma = std::isfinite(map.gamma) ? std::clamp(map.gamma, 0.2, 5.0) : 1.0;
  return gamma == 1.0 ? travel : std::pow(travel, gamma);
}

// The controls of one device state; the press counters are carried from `previous`.
inline Controls MapControls(const MappingConfig& config, const std::vector<int16_t>& axes,
                            const std::vector<uint8_t>& buttons, const Controls& previous) {
  const auto axis = [&](int index, int16_t fallback) {
    return index >= 0 && size_t(index) < axes.size() ? axes[size_t(index)] : fallback;
  };
  const auto button = [&](int index) {
    return index >= 0 && size_t(index) < buttons.size() && buttons[size_t(index)] != 0;
  };
  const auto pedal = [&](const PedalMap& map) {
    return map.axis < 0 || size_t(map.axis) >= axes.size() ? 0.0 : MapPedal(map, axes[map.axis]);
  };
  Controls out;
  out.connected = true;
  out.steering = MapSteering(config.steering, axis(config.steering.axis, 0));
  out.throttle = pedal(config.throttle);
  out.brake = pedal(config.brake);
  out.clutch = std::max(pedal(config.clutch), button(config.clutch_button) ? 1.0 : 0.0);
  out.handbrake = button(config.handbrake_button) ? 1.0 : 0.0;
  out.shift_up = button(config.shift_up_button);
  out.shift_down = button(config.shift_down_button);
  out.shift_up_presses = previous.shift_up_presses + (out.shift_up && !previous.shift_up ? 1 : 0);
  out.shift_down_presses =
      previous.shift_down_presses + (out.shift_down && !previous.shift_down ? 1 : 0);
  return out;
}

}  // namespace rex::input::wheel
