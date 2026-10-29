/**
 * @file        input/wheel/ffb_stream.h
 * @brief       Playback of a force source that arrives in bursts at its own rate
 *
 * Header only and free of runtime and SDL dependencies, so the CPU tests
 * (work/fm4-rex/tests/wheel-ffb-tests.cpp) check the same logic the runtime
 * runs. See docs/analysis/racing-wheels.md, stage 4.
 *
 * The title's physics computes its steps in bursts: each frame runs the steps
 * that the frame's time covers, one after another. Each step's force carries
 * its simulation time. The force thread plays the points back on its own
 * clock, a little behind, so the steps of a burst are spread over the time
 * they stand for and the force follows the physics, not the frame rate:
 *
 * - The offset maps simulation time to host time: the smallest gap between a
 *   point's arrival and its simulation time over the last window (the point
 *   that came the soonest after it was due).
 * - The delay is the largest lateness beyond that over the window (at most
 *   max_delay_s), so every point of a burst has arrived when its time comes.
 * - The value at the playback time is interpolated between the two points
 *   around it, and held at the newest point when playback runs ahead.
 *
 * Time is in seconds; the host's clock and the simulation's need not share an
 * origin.
 */

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace rex::input::wheel {

struct StreamConfig {
  // The span over which the offset and the delay are measured.
  double window_s = 0.5;
  // The longest delay; a burst later than this is played late (held, then caught up).
  double max_delay_s = 0.05;
  // Added to the measured delay, for the jitter of the bursts' arrival.
  double margin_s = 0.001;
  // The time constant that steers the playback clock towards its target.
  double steer_s = 0.25;
};

class ForceStream {
 public:
  explicit ForceStream(const StreamConfig& config = {}) : config_(config) {}

  // A point: its simulation time, its value, and when it arrived on the host's clock. Points
  // come in simulation order; one that goes back (a rewind, a new session) starts over.
  void Add(double sim_s, double value, double arrival_s) {
    if (!std::isfinite(sim_s) || !std::isfinite(value) || !std::isfinite(arrival_s)) {
      return;
    }
    if (count_ > 0 && sim_s <= points_[Index(count_ - 1)].sim_s) {
      if (sim_s < points_[Index(count_ - 1)].sim_s) {
        Reset();
      } else {
        return;
      }
    }
    if (count_ - first_ >= kPoints) {
      ++first_;
    }
    points_[Index(count_)] = {sim_s, value, arrival_s};
    ++count_;
    last_arrival_s_ = arrival_s;
  }

  void Reset() {
    count_ = 0;
    first_ = 0;
    has_play_ = false;
  }

  // The value at host time `now_s`; false while there are no points.
  bool Sample(double now_s, double* value) {
    // Points older than the window are dropped, but two always stay to interpolate.
    while (count_ - first_ > 2 && points_[Index(first_)].arrival_s < now_s - config_.window_s) {
      ++first_;
    }
    if (count_ == first_) {
      return false;
    }
    double offset = INFINITY;
    for (uint64_t i = first_; i < count_; ++i) {
      const Point& p = points_[Index(i)];
      offset = std::min(offset, p.arrival_s - p.sim_s);
    }
    double delay = 0.0;
    for (uint64_t i = first_; i < count_; ++i) {
      const Point& p = points_[Index(i)];
      delay = std::max(delay, p.arrival_s - p.sim_s - offset);
    }
    delay_s_ = std::min(delay + config_.margin_s, config_.max_delay_s);
    // The playback clock runs with the host's and is steered towards the target, so the
    // window's steps (a point entering or leaving it) do not jolt it; a large error snaps it.
    const double target = now_s - offset - delay_s_;
    const double dt = has_play_ ? std::clamp(now_s - last_now_s_, 0.0, 0.1) : 0.0;
    last_now_s_ = now_s;
    if (!has_play_ || std::fabs(play_s_ + dt - target) > config_.max_delay_s) {
      play_s_ = target;
      has_play_ = true;
    } else {
      play_s_ += dt;
      // Ahead of the target, playback may run out of points: it gives way faster.
      const double steer = target < play_s_ ? config_.steer_s * 0.1 : config_.steer_s;
      play_s_ += (target - play_s_) * std::min(1.0, dt / steer);
    }
    const double play = play_s_;
    const Point& newest = points_[Index(count_ - 1)];
    held_ = play >= newest.sim_s;
    if (held_) {
      *value = newest.value;
      return true;
    }
    const Point& oldest = points_[Index(first_)];
    if (play <= oldest.sim_s) {
      *value = oldest.value;
      return true;
    }
    for (uint64_t i = count_ - 1; i > first_; --i) {
      const Point& a = points_[Index(i - 1)];
      const Point& b = points_[Index(i)];
      if (play >= a.sim_s) {
        const double span = b.sim_s - a.sim_s;
        *value = span > 0.0 ? a.value + (b.value - a.value) * (play - a.sim_s) / span : b.value;
        return true;
      }
    }
    *value = oldest.value;
    return true;
  }

  // When the newest point arrived (for the watchdog), and the delay of the last Sample.
  double last_arrival_s() const { return last_arrival_s_; }
  double delay_s() const { return delay_s_; }
  bool empty() const { return count_ == first_; }
  // Whether the last Sample ran ahead of the newest point and held it.
  bool held() const { return held_; }

 private:
  struct Point {
    double sim_s = 0.0;
    double value = 0.0;
    double arrival_s = 0.0;
  };
  // A window of 0.5 s at 360 points a second, with room to spare.
  static constexpr size_t kPoints = 1024;

  size_t Index(uint64_t i) const { return size_t(i % kPoints); }

  StreamConfig config_;
  std::array<Point, kPoints> points_{};
  uint64_t count_ = 0;
  uint64_t first_ = 0;
  double last_arrival_s_ = -INFINITY;
  double delay_s_ = 0.0;
  double play_s_ = 0.0;
  double last_now_s_ = 0.0;
  bool has_play_ = false;
  bool held_ = false;
};

}  // namespace rex::input::wheel
