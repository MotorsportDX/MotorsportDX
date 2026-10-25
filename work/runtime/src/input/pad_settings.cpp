/**
 * @file        input/pad_settings.cpp
 * @brief       The physical pads' shaping and rumble cvars
 *
 * The defaults keep the pads' values as they are; the stick and trigger
 * deadzones of the title's profile still apply after these. See
 * docs/analysis/modern-gamepads.md.
 */

#include <algorithm>

#include <rex/cvar.h>
#include <rex/input/pad_settings.h>

REXCVAR_DEFINE_DOUBLE(pad_stick_deadzone, 0.0, "Input",
                      "Physical pads: radial deadzone of both sticks, a fraction of full "
                      "deflection; the rest is rescaled so there is no jump")
    .range(0.0, 0.5);
REXCVAR_DEFINE_DOUBLE(pad_stick_outer, 1.0, "Input",
                      "Physical pads: the stick deflection that gives full output")
    .range(0.5, 1.0);
REXCVAR_DEFINE_DOUBLE(pad_stick_curve, 1.0, "Input",
                      "Physical pads: exponent on the stick deflection (1: linear, above 1: "
                      "finer near the centre)")
    .range(0.5, 3.0);
REXCVAR_DEFINE_DOUBLE(pad_steering_anti_deadzone, 0.0, "Input",
                      "Physical pads, left stick: any deflection starts at this fraction, past "
                      "the game's own steering deadzone (0.24 in its default profile). Adds up "
                      "with a lowered in-game deadzone")
    .range(0.0, 0.5);
REXCVAR_DEFINE_DOUBLE(pad_trigger_deadzone, 0.0, "Input",
                      "Physical pads: triggers read 0 up to this fraction")
    .range(0.0, 0.5);
REXCVAR_DEFINE_DOUBLE(pad_trigger_outer, 1.0, "Input",
                      "Physical pads: the trigger travel that gives full output")
    .range(0.5, 1.0);
REXCVAR_DEFINE_DOUBLE(pad_rumble_strength, 1.0, "Input",
                      "Physical pads: multiplies the game's two rumble motors (clamped at full)")
    .range(0.0, 2.0);
REXCVAR_DEFINE_BOOL(pad_trigger_rumble, false, "Input",
                    "Physical pads with trigger motors (SDL): rumble the throttle trigger from "
                    "the game's left motor and the brake trigger from its right motor, scaled by "
                    "how far each is pressed");
REXCVAR_DEFINE_DOUBLE(pad_trigger_rumble_strength, 1.0, "Input",
                      "Physical pads: multiplies the derived trigger rumble (clamped at full)")
    .range(0.0, 2.0);

namespace rex::input {

PadSettings CurrentPadSettings() {
  PadSettings settings;
  StickShaping stick;
  stick.deadzone = std::clamp(REXCVAR_GET(pad_stick_deadzone), 0.0, 0.5);
  stick.outer = std::clamp(REXCVAR_GET(pad_stick_outer), 0.5, 1.0);
  stick.curve = std::clamp(REXCVAR_GET(pad_stick_curve), 0.5, 3.0);
  settings.right_stick = stick;
  stick.anti_deadzone = std::clamp(REXCVAR_GET(pad_steering_anti_deadzone), 0.0, 0.5);
  settings.left_stick = stick;
  settings.triggers.deadzone = std::clamp(REXCVAR_GET(pad_trigger_deadzone), 0.0, 0.5);
  settings.triggers.outer = std::clamp(REXCVAR_GET(pad_trigger_outer), 0.5, 1.0);
  settings.rumble_strength = std::clamp(REXCVAR_GET(pad_rumble_strength), 0.0, 2.0);
  settings.trigger_rumble = REXCVAR_GET(pad_trigger_rumble);
  settings.trigger_rumble_strength = std::clamp(REXCVAR_GET(pad_trigger_rumble_strength), 0.0, 2.0);
  return settings;
}

void ShapeGamepad(X_INPUT_GAMEPAD& gamepad, const PadSettings& settings) {
  if (!settings.left_stick.IsIdentity()) {
    int16_t x = static_cast<int16_t>(gamepad.thumb_lx);
    int16_t y = static_cast<int16_t>(gamepad.thumb_ly);
    ShapeStick(x, y, settings.left_stick);
    gamepad.thumb_lx = x;
    gamepad.thumb_ly = y;
  }
  if (!settings.right_stick.IsIdentity()) {
    int16_t x = static_cast<int16_t>(gamepad.thumb_rx);
    int16_t y = static_cast<int16_t>(gamepad.thumb_ry);
    ShapeStick(x, y, settings.right_stick);
    gamepad.thumb_rx = x;
    gamepad.thumb_ry = y;
  }
  gamepad.left_trigger = ShapeTrigger(gamepad.left_trigger, settings.triggers);
  gamepad.right_trigger = ShapeTrigger(gamepad.right_trigger, settings.triggers);
}

}  // namespace rex::input
