/**
 * @file        input/wheel/wheel_controls.h
 * @brief       The racing wheel's driving controls, published for a title integration
 *
 * The SDL input driver's wheel host publishes the mapped controls
 * (wheel_mapping.h) at its rate; a title integration reads the latest ones
 * where the title reads its driving actions (FM4: work/fm4-rex/src/
 * fm4-wheel-input.cpp). The lock is the store's own, never the input
 * driver's, so a guest hook may read from any thread. See
 * docs/analysis/racing-wheels.md, stage 2.
 */

#pragma once

#include <rex/input/wheel/wheel_mapping.h>

namespace rex::input::wheel {

void PublishControls(const Controls& controls);
// The latest controls; false (and `out` disconnected) while no wheel drives.
bool GetControls(Controls* out);

}  // namespace rex::input::wheel
