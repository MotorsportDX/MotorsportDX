/**
 * @file        input/title_input_ranges.h
 * @brief       The pad ranges a title applies to the axes the keyboard drives
 *
 * A title integration publishes them from the title's own input conversion
 * (FM4: the profile's deadzones, work/fm4-rex/src/fm4-ui-diagnostics.cpp), so
 * the keyboard and mouse driver can map its 0..1 values into them and the
 * title's output is linear from the first key press. Until a user's ranges are
 * published the defaults apply, which are the identity unless the integration
 * sets its own. See docs/analysis/keyboard-controls.md.
 */

#pragma once

#include <cstdint>

#include <rex/input/keyboard_model.h>

namespace rex::input {

void SetTitleInputRanges(uint32_t user_index, const keyboard::TitleRanges& ranges);
void SetDefaultTitleInputRanges(const keyboard::TitleRanges& ranges);
keyboard::TitleRanges GetTitleInputRanges(uint32_t user_index);

}  // namespace rex::input
