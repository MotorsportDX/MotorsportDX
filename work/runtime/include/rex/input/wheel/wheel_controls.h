/**
 * @file        input/wheel/wheel_controls.h
 * @brief       The racing wheel's exchange with a title integration
 *
 * The SDL input driver's wheel host publishes the mapped controls
 * (wheel_mapping.h) at its rate; a title integration reads the latest ones
 * where the title reads its driving actions (FM4: work/fm4-rex/src/
 * fm4-wheel-input.cpp). The other way, the integration publishes the title's
 * force feedback once a frame, and the host's force thread takes the latest
 * sample; and the force at the title's physics rate, a point per physics step,
 * which the force thread plays back (ffb_stream.h). The locks are the
 * stores' own, never the input driver's, so a guest hook may publish or read
 * from any thread. See docs/analysis/racing-wheels.md, stages 2 to 5.
 */

#pragma once

#include <cstddef>
#include <cstdint>

#include <rex/input/wheel/ffb_governor.h>
#include <rex/input/wheel/wheel_mapping.h>

namespace rex::input::wheel {

void PublishControls(const Controls& controls);
// The latest controls; false (and `out` disconnected) while no wheel drives.
bool GetControls(Controls* out);

// The road-wheel lock of the car the wheel drives, in degrees at the title's full steering, which
// sets the car's own steering lock (wheel_mapping.h, stage 5); 0: none published yet.
void PublishRoadLock(double degrees);
double GetRoadLock();

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
  // The road's vibration (kerbs, rumble strips, grass, gravel), 0..1 before the host's gains:
  // the title's rumble level for its surface bumpiness, held until the next sample (stage 4).
  double road = 0.0;
  // The latest impact (walls, cars, objects), 0..1 before the host's gains, and how long the
  // title lets it rumble; `impacts` counts them, and a new count starts a burst.
  double impact = 0.0;
  double impact_s = 0.0;
  uint32_t impacts = 0;
};

void PublishForces(const ForceSample& sample);
// The latest sample; returns its sequence number, which grows with each publish (0: none yet).
uint64_t GetForces(ForceSample* out);

// The title's force at its physics rate (stage 4): one point per physics step of the player's
// car, published as the steps run (in bursts, one per frame). The frame's sample still decides
// whether the player drives, and carries the conditions and effects.
struct ForcePoint {
  // The step's simulation time, seconds on the title's clock.
  double sim_s = 0.0;
  // The constant force, -1..1 of full scale, scaled as the frame sample's.
  double constant = 0.0;
  // The title's steering torque of the step, for the recording.
  double torque = 0.0;
};

void PublishForcePoint(const ForcePoint& point);
// The points published after the first `after` (the count a previous call returned), at most
// `max`, oldest first, into `out`; `got` is how many. Returns how many were ever published.
// Points older than the store's ring (512) are lost.
uint64_t GetForcePoints(uint64_t after, ForcePoint* out, size_t max, size_t* got);

}  // namespace rex::input::wheel
