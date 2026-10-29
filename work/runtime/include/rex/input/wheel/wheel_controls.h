/**
 * @file        input/wheel/wheel_controls.h
 * @brief       The racing wheel's exchange with a title integration
 *
 * The SDL input driver's wheel host publishes the mapped controls
 * (wheel_mapping.h) at its rate; a title integration reads the latest ones
 * where the title reads its driving actions (FM4: work/fm4-rex/src/
 * fm4-wheel-input.cpp). The other way, the integration publishes the title's
 * force feedback once a frame, and the host's force thread takes the latest
 * sample. The locks are the
 * stores' own, never the input driver's, so a guest hook may publish or read
 * from any thread. See docs/analysis/racing-wheels.md, stages 2 and 3.
 */

#pragma once

#include <cstdint>

#include <rex/input/wheel/ffb_governor.h>
#include <rex/input/wheel/wheel_mapping.h>

namespace rex::input::wheel {

void PublishControls(const Controls& controls);
// The latest controls; false (and `out` disconnected) while no wheel drives.
bool GetControls(Controls* out);

// The title's force feedback for one frame.
struct ForceSample {
  // False outside a driven race (no player's car under the player's control): the host zeroes
  // every force at once. A source that stops publishing (pause, menus, loading) is caught by the
  // governor's watchdog instead.
  bool driving = false;
  // The constant force, -1..1 of full scale, and the condition effects, before the host's gain
  // and cap.
  double constant = 0.0;
  Condition spring;
  Condition damper;
  // The title's own values the forces come from, for the recording: its steering torque, its
  // speed and its rear slip.
  double torque = 0.0;
  double speed = 0.0;
  double slip = 0.0;
};

void PublishForces(const ForceSample& sample);
// The latest sample; returns its sequence number, which grows with each publish (0: none yet).
uint64_t GetForces(ForceSample* out);

}  // namespace rex::input::wheel
