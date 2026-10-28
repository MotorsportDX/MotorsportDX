/**
 * @file        input/wheel/ffb_governor.h
 * @brief       The force feedback safety governor of the racing wheel
 *
 * Header only and free of runtime and SDL dependencies, so the CPU tests
 * (work/fm4-rex/tests/wheel-ffb-tests.cpp) check the same logic the runtime
 * and the launcher's wheel test run. Every force sent to a wheel passes
 * through it; see docs/analysis/racing-wheels.md, "Force feedback safety".
 *
 * Forces are fractions of the device's full scale, -1..1 (SDL's constant
 * force level / 32767). Time is in seconds on any monotonic clock.
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace rex::input::wheel {

struct GovernorConfig {
  // The hard upper limit of the output, independent of the gain. The default
  // stays well below a direct-drive base's peak until the owner sets it
  // (racing-wheels.md, open question 6).
  double cap = 0.25;
  // Multiplies the source's force before the ramp, the slew limit and the cap.
  double gain = 1.0;
  // The largest change of the output per second, in full scale (4: zero to full
  // in 250 ms). A larger step from the source is spread over several ticks.
  double slew_per_s = 4.0;
  // The ramp-in after every start and every zero state.
  double ramp_s = 1.5;
  // No new source value for this long: the output falls to zero (at the slew
  // limit) and ramps in again when values return.
  double watchdog_s = 0.05;
  // The longest tick the slew limit accounts for, so a stalled force thread
  // cannot jump on its next tick.
  double max_tick_s = 0.01;
};

// The reasons the output is held at zero at once (no slew). Any set bit zeroes it.
enum BlockReason : uint32_t {
  kBlockNone = 0,
  kBlockFocus = 1u << 0,     // the window lost focus
  kBlockPaused = 1u << 1,    // pause, menus, loading, replays, photo mode, no human car
  kBlockExit = 1u << 2,      // shutdown, or an emergency stop (crash)
  kBlockNoDevice = 1u << 3,  // nothing to send to yet
  kBlockUser = 1u << 4,      // the user stopped the force (the launcher's test)
};

// What the governor did on the last tick, for the recording and the log.
enum GovernorFlag : uint32_t {
  kFlagBlocked = 1u << 0,
  kFlagWatchdog = 1u << 1,
  kFlagRamping = 1u << 2,
  kFlagSlewLimited = 1u << 3,
  kFlagCapped = 1u << 4,
  kFlagBadValue = 1u << 5,  // the last source value was NaN, infinite or out of range
};

class Governor {
 public:
  explicit Governor(const GovernorConfig& config = {}) : config_(Sanitize(config)) {}

  void Configure(const GovernorConfig& config) { config_ = Sanitize(config); }
  const GovernorConfig& config() const { return config_; }

  // A new value from the force source. NaN, infinities and values outside
  // -1..1 become zero (the title clamps its own torque to -1..1).
  void Submit(double force, double now_s) {
    bad_value_ = !std::isfinite(force) || std::fabs(force) > 1.0;
    target_ = bad_value_ ? 0.0 : force;
    last_submit_s_ = now_s;
    has_submit_ = true;
  }

  void SetBlocked(uint32_t reason, bool blocked) {
    blocked_ = blocked ? (blocked_ | reason) : (blocked_ & ~reason);
  }
  uint32_t blocked() const { return blocked_; }

  // The output for this tick.
  double Step(double now_s) {
    const double dt = has_tick_ ? std::clamp(now_s - last_tick_s_, 0.0, config_.max_tick_s) : 0.0;
    last_tick_s_ = now_s;
    has_tick_ = true;
    flags_ = bad_value_ ? kFlagBadValue : 0u;

    if (blocked_ != kBlockNone) {
      // Removing force is always safe: zero at once, and ramp in again later.
      output_ = 0.0;
      envelope_ = 0.0;
      flags_ |= kFlagBlocked;
      return output_;
    }

    const bool stale = !has_submit_ || now_s - last_submit_s_ > config_.watchdog_s;
    double wanted;
    if (stale) {
      flags_ |= kFlagWatchdog;
      wanted = 0.0;
      // The ramp starts over once values return.
      envelope_ = 0.0;
    } else {
      envelope_ = config_.ramp_s > 0.0 ? std::min(1.0, envelope_ + dt / config_.ramp_s) : 1.0;
      if (envelope_ < 1.0) {
        flags_ |= kFlagRamping;
      }
      wanted = target_ * config_.gain * envelope_;
    }

    if (std::fabs(wanted) > config_.cap) {
      wanted = std::copysign(config_.cap, wanted);
      flags_ |= kFlagCapped;
    }
    const double step = config_.slew_per_s * dt;
    const double delta = wanted - output_;
    if (std::fabs(delta) > step) {
      output_ += std::copysign(step, delta);
      flags_ |= kFlagSlewLimited;
    } else {
      output_ = wanted;
    }
    return output_;
  }

  // The fraction of the ramp-in done, 0..1 (0 while blocked or stale). Condition
  // effects that the firmware runs (spring, damper) scale by it too.
  double envelope() const { return envelope_; }
  double output() const { return output_; }
  double target() const { return target_; }
  uint32_t flags() const { return flags_; }

  // A level for a condition effect (a spring's or damper's coefficient), bounded
  // like the constant force: zero while blocked, ramped in, and capped.
  double ConditionLevel(double requested) const {
    if (blocked_ != kBlockNone || !std::isfinite(requested)) {
      return 0.0;
    }
    return std::clamp(requested * config_.gain, 0.0, config_.cap) * envelope_;
  }

  static GovernorConfig Sanitize(GovernorConfig config) {
    const auto finite_or = [](double value, double fallback) {
      return std::isfinite(value) ? value : fallback;
    };
    config.cap = std::clamp(finite_or(config.cap, 0.0), 0.0, 1.0);
    config.gain = std::clamp(finite_or(config.gain, 0.0), 0.0, 4.0);
    config.slew_per_s = std::clamp(finite_or(config.slew_per_s, 4.0), 0.1, 100.0);
    config.ramp_s = std::clamp(finite_or(config.ramp_s, 1.5), 0.0, 10.0);
    config.watchdog_s = std::clamp(finite_or(config.watchdog_s, 0.05), 0.005, 1.0);
    config.max_tick_s = std::clamp(finite_or(config.max_tick_s, 0.01), 0.0001, 0.1);
    return config;
  }

 private:
  GovernorConfig config_;
  double target_ = 0.0;
  double output_ = 0.0;
  double envelope_ = 0.0;
  double last_submit_s_ = 0.0;
  double last_tick_s_ = 0.0;
  bool has_submit_ = false;
  bool has_tick_ = false;
  bool bad_value_ = false;
  uint32_t blocked_ = kBlockNone;
  uint32_t flags_ = 0;
};

}  // namespace rex::input::wheel
