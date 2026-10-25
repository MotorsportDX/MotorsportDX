/**
 * @file        input/pad_settings.h
 * @brief       The physical pads' shaping and rumble settings from the cvars
 *
 * The pad_* cvars of the Input category (src/input/pad_settings.cpp) read
 * into a PadSettings, and its application to a guest pad state. Used by the
 * physical drivers (sdl, xinput) only. See docs/analysis/modern-gamepads.md.
 */

#pragma once

#include <rex/input/input.h>
#include <rex/input/pad_shaping.h>

namespace rex::input {

// The current values of the pad_* cvars, read on every call (they reload
// while the game runs).
PadSettings CurrentPadSettings();

// Shapes the sticks and triggers of a state a physical driver returns. A
// no-op at the defaults.
void ShapeGamepad(X_INPUT_GAMEPAD& gamepad, const PadSettings& settings);

// The steering anti-deadzone (pad_steering_anti_deadzone): maps a physical
// pad's left stick into the steering deadzone the title reports for this guest
// user (title_input_ranges.h). InputSystem applies it to each physical device
// before the merge, so a pad and the keyboard, which maps into the same range,
// compare in the title's terms. A no-op while the cvar is off.
void MapPadSteering(X_INPUT_GAMEPAD& gamepad, uint32_t user_index);

}  // namespace rex::input
