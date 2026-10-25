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

}  // namespace rex::input
