/**
 * @file        input/pad_shaping.h
 * @brief       Stick and trigger shaping and rumble scaling for physical pads
 *
 * Header only and free of runtime dependencies, so the CPU tests
 * (work/fm4-rex/tests/pad-shaping-tests.cpp) check the same math as the
 * runtime. The physical drivers (sdl, xinput) apply it to what they return;
 * the diagnostic controller and the keyboard and mouse driver do not. Every
 * function returns its input unchanged at the defaults. See
 * docs/analysis/modern-gamepads.md.
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

namespace rex::input {

// A stick, in fractions of full deflection (32767) along the stick's
// direction, like the title's own radial deadzone (sub_822B3F18).
struct StickShaping {
  // Magnitudes up to this read as centred.
  double deadzone = 0.0;
  // The magnitude that gives full deflection; the range from the deadzone's
  // edge to here is rescaled to 0..1, so there is no jump at the edge.
  double outer = 1.0;
  // Exponent on the rescaled magnitude below 1 (1: linear).
  double curve = 1.0;
  // Any non-zero magnitude starts here: lifts the stick past a deadzone the
  // title applies after it (the profile's steering deadzone is 0.24).
  double anti_deadzone = 0.0;

  bool IsIdentity() const {
    return deadzone == 0.0 && outer == 1.0 && curve == 1.0 && anti_deadzone == 0.0;
  }
};

// An 8-bit trigger, in fractions of 255.
struct TriggerShaping {
  double deadzone = 0.0;
  double outer = 1.0;

  bool IsIdentity() const { return deadzone == 0.0 && outer == 1.0; }
};

inline int16_t ClampStickAxis(double value) {
  return static_cast<int16_t>(std::clamp(std::lround(value), -32768L, 32767L));
}

// Shapes one stick in place. The direction is kept; only the magnitude
// changes. Diagonals past full deflection (up to about 1.41) keep growing
// linearly, so each axis can still reach its end, as on the raw stick.
inline void ShapeStick(int16_t& x, int16_t& y, const StickShaping& shaping) {
  if (shaping.IsIdentity()) {
    return;
  }
  const double fx = x;
  const double fy = y;
  const double magnitude = std::sqrt(fx * fx + fy * fy) / 32767.0;
  if (magnitude == 0.0 || magnitude <= shaping.deadzone) {
    x = 0;
    y = 0;
    return;
  }
  const double span = std::max(shaping.outer - shaping.deadzone, 0.01);
  double shaped = (magnitude - shaping.deadzone) / span;
  if (shaped < 1.0 && shaping.curve != 1.0) {
    shaped = std::pow(shaped, shaping.curve);
  }
  if (shaping.anti_deadzone > 0.0) {
    shaped = shaping.anti_deadzone + shaped * (1.0 - shaping.anti_deadzone);
  }
  const double scale = shaped / magnitude;
  x = ClampStickAxis(fx * scale);
  y = ClampStickAxis(fy * scale);
}

inline uint8_t ShapeTrigger(uint8_t value, const TriggerShaping& shaping) {
  if (shaping.IsIdentity()) {
    return value;
  }
  const double fraction = value / 255.0;
  if (fraction <= shaping.deadzone) {
    return 0;
  }
  const double span = std::max(shaping.outer - shaping.deadzone, 0.01);
  const double shaped = std::min(1.0, (fraction - shaping.deadzone) / span);
  return static_cast<uint8_t>(std::lround(shaped * 255.0));
}

inline uint16_t ScaleMotor(uint16_t speed, double strength) {
  if (strength == 1.0) {
    return speed;
  }
  return static_cast<uint16_t>(std::clamp(std::lround(speed * strength), 0L, 65535L));
}

// How far a trigger is pressed past XInput's trigger threshold (30, which the
// title also uses before a profile loads), 0..1.
inline double TriggerPressure(uint8_t value) {
  constexpr double kThreshold = 30.0;
  return value <= kThreshold ? 0.0 : (value - kThreshold) / (255.0 - kThreshold);
}

// Trigger rumble from the title's two motors and the triggers it read
// (docs/analysis/modern-gamepads.md, "Deriving trigger rumble"): the left
// trigger (brake) from the right motor, which carries front tyre slip, the
// right trigger (throttle) from the left motor, which carries rear tyre slip,
// each scaled by its trigger's pressure. Zero motors or released triggers give
// zero. Returns {left trigger, right trigger}.
inline std::pair<uint16_t, uint16_t> DeriveTriggerRumble(uint16_t left_motor, uint16_t right_motor,
                                                         uint8_t left_trigger,
                                                         uint8_t right_trigger, double strength) {
  auto derive = [&](uint16_t motor, uint8_t trigger) {
    const double value = motor * TriggerPressure(trigger) * strength;
    return static_cast<uint16_t>(std::clamp(std::lround(value), 0L, 65535L));
  };
  return {derive(right_motor, left_trigger), derive(left_motor, right_trigger)};
}

// Everything a physical driver applies, read from the cvars by
// CurrentPadSettings (rex/input/pad_settings.h).
struct PadSettings {
  // The left stick steers in the title's default layout; the anti-deadzone is
  // its own.
  StickShaping left_stick;
  StickShaping right_stick;
  TriggerShaping triggers;
  double rumble_strength = 1.0;
  bool trigger_rumble = false;
  double trigger_rumble_strength = 1.0;
};

}  // namespace rex::input
