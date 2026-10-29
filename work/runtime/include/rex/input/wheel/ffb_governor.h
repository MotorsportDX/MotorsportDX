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
 *
 * A source submits a constant force and, optionally, two condition effects
 * that the firmware runs (a centring spring and a damper). The cap bounds
 * their sum: the constant force plus the conditions' saturations, the most
 * force the firmware can apply at once. The conditions take at most a share of
 * the cap (both shrink by the same factor beyond it), and the constant force
 * gets the rest. While a condition's level moves, it is passed on only in
 * steps of at least 0.002 (and exactly once it settles), so a device update
 * follows only a real change, and the bound holds for the levels the device
 * actually has.
 *
 * Two periodic effects that the firmware runs count in the same sum by their
 * magnitude (stage 4): the road's vibration, a level held like a condition's,
 * and impacts, bursts of a fixed magnitude and length. They take at most their
 * own share of the cap; an impact keeps its magnitude for its length and the
 * road's vibration gets what it leaves.
 *
 * The soft lock's end stop (stage 5, EndStop below) is a force the host
 * computes from the wheel's angle. It joins the constant force after the
 * title's gain, minimum force and smoothing, with its own strength, and goes
 * through the same ramp, watchdog, cap and slew limit.
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
  // The smallest constant force sent while the source asks for any, a fraction of full scale
  // after the gain: lifts small forces above a wheel's own friction. Below 2% of full scale the
  // force rises linearly to it, so the force stays continuous through zero.
  double min_force = 0.0;
  // The time constant of a low-pass on the source's constant force (0: none): smooths the steps
  // of a source that changes once a frame.
  double smoothing_s = 0.0;
  // The largest share of the cap the condition effects take together; the constant force has
  // at least the rest.
  double condition_share = 0.5;
  // The largest share of the cap the periodic effects (the road's vibration and impacts) take
  // together.
  double effect_share = 0.25;
};

// A condition effect the firmware runs, as fractions of full scale: the coefficient (force per
// unit of position for a spring, of velocity for a damper) and the saturation (the most force
// the effect applies).
struct Condition {
  double coefficient = 0.0;
  double saturation = 0.0;
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

  // A new value from the force source: the constant force and the condition effects. NaN,
  // infinities and values outside -1..1 (0..1 for the conditions) become zero (the title clamps
  // its own torque to -1..1).
  void Submit(double force, double now_s, const Condition& spring = {},
              const Condition& damper = {}) {
    bad_value_ = !std::isfinite(force) || std::fabs(force) > 1.0;
    target_ = bad_value_ ? 0.0 : force;
    spring_target_ = CleanCondition(spring);
    damper_target_ = CleanCondition(damper);
    last_submit_s_ = now_s;
    has_submit_ = true;
  }

  // The road's vibration, a periodic effect's magnitude 0..1 before the gain, held until the
  // next value (the watchdog of Submit applies).
  void SubmitRoad(double level) { road_target_ = CleanLevel(level); }

  // The end stop's force (EndStop), -1..1, added to the constant force on each tick until the
  // next value; zero while the source is stale or blocked, like every force.
  void SubmitEndStop(double force) {
    const bool bad = !std::isfinite(force) || std::fabs(force) > 1.0;
    bad_value_ = bad_value_ || bad;
    end_stop_target_ = bad ? 0.0 : force;
  }

  // An impact: a burst of `level` (0..1 before the gain) for `length_s` from now. A new impact
  // replaces the one that plays.
  void Impact(double level, double length_s, double now_s) {
    impact_wanted_ = CleanLevel(level);
    impact_length_s_ = std::isfinite(length_s) ? std::clamp(length_s, 0.0, 2.0) : 0.0;
    impact_start_s_ = now_s;
    impact_pending_ = impact_wanted_ > 0.0 && impact_length_s_ > 0.0;
  }

  void SetBlocked(uint32_t reason, bool blocked) {
    blocked_ = blocked ? (blocked_ | reason) : (blocked_ & ~reason);
  }
  uint32_t blocked() const { return blocked_; }

  // The output for this tick: the constant force; spring() and damper() hold the conditions'.
  double Step(double now_s) {
    const double dt = has_tick_ ? std::clamp(now_s - last_tick_s_, 0.0, config_.max_tick_s) : 0.0;
    last_tick_s_ = now_s;
    has_tick_ = true;
    flags_ = bad_value_ ? kFlagBadValue : 0u;

    if (blocked_ != kBlockNone) {
      // Removing force is always safe: zero at once, and ramp in again later.
      output_ = 0.0;
      envelope_ = 0.0;
      filtered_ = 0.0;
      end_stop_ = 0.0;
      spring_ = {};
      damper_ = {};
      road_ = 0.0;
      StopImpact();
      flags_ |= kFlagBlocked;
      return output_;
    }

    const bool stale = !has_submit_ || now_s - last_submit_s_ > config_.watchdog_s;
    double wanted = 0.0;
    Condition spring, damper;
    double road = 0.0;
    end_stop_ = 0.0;
    if (stale) {
      flags_ |= kFlagWatchdog;
      // The ramp starts over once values return.
      envelope_ = 0.0;
      filtered_ = 0.0;
    } else {
      envelope_ = config_.ramp_s > 0.0 ? std::min(1.0, envelope_ + dt / config_.ramp_s) : 1.0;
      if (envelope_ < 1.0) {
        flags_ |= kFlagRamping;
      }
      // The low-pass starts from zero after every zero state, as the force does.
      if (config_.smoothing_s > 0.0) {
        filtered_ += (target_ - filtered_) * (1.0 - std::exp(-dt / config_.smoothing_s));
      } else {
        filtered_ = target_;
      }
      wanted = MinForce(filtered_ * config_.gain) * envelope_;
      end_stop_ = end_stop_target_ * envelope_;
      wanted += end_stop_;
      spring = Scaled(spring_target_);
      damper = Scaled(damper_target_);
      road = std::clamp(road_target_ * config_.gain, 0.0, 1.0) * envelope_;
    }

    const double step = config_.slew_per_s * dt;
    // The conditions first, within their share of the cap; they change at the slew limit, and
    // only by a real step.
    const double share = config_.cap * config_.condition_share;
    const double conditions = spring.saturation + damper.saturation;
    if (conditions > share) {
      const double k = conditions > 0.0 ? share / conditions : 0.0;
      spring = {spring.coefficient * k, spring.saturation * k};
      damper = {damper.coefficient * k, damper.saturation * k};
      flags_ |= kFlagCapped;
    }
    spring_ = Hold(spring_, spring, spring_wanted_, step);
    damper_ = Hold(damper_, damper, damper_wanted_, step);
    // The periodic effects within their share: an impact at its start, for its whole length
    // (none while stale); the road's vibration in what it leaves, held like a condition's level
    // and cut at once when an impact needs its room.
    const double effects = config_.cap * config_.effect_share;
    if (stale) {
      StopImpact();
    } else if (impact_pending_) {
      impact_pending_ = false;
      const double level = std::clamp(impact_wanted_ * config_.gain, 0.0, 1.0) * envelope_;
      impact_ = std::min(level, effects);
      if (impact_ < level) flags_ |= kFlagCapped;
      if (impact_ > 0.0) ++impacts_;
    }
    if (impact_ > 0.0 && now_s - impact_start_s_ >= impact_length_s_) {
      impact_ = 0.0;
    }
    const double road_room = std::max(0.0, effects - impact_);
    if (road > road_room) {
      road = road_room;
      flags_ |= kFlagCapped;
    }
    road_ = std::min(HoldLevel(road_, road, road_wanted_, step), road_room);
    // The constant force gets what the conditions and the periodic effects leave of the cap.
    const double room = std::max(
        0.0, config_.cap - spring_.saturation - damper_.saturation - road_ - impact_);
    if (std::fabs(wanted) > room) {
      wanted = std::copysign(room, wanted);
      flags_ |= kFlagCapped;
    }
    output_ = Slew(output_, wanted, step);
    // Growing conditions leave less room at once: the force gives way by at most their step.
    if (std::fabs(output_) > room) {
      output_ = std::copysign(room, output_);
    }
    return output_;
  }

  // The fraction of the ramp-in done, 0..1 (0 while blocked or stale).
  double envelope() const { return envelope_; }
  double output() const { return output_; }
  double target() const { return target_; }
  // The end stop's share of this tick's wanted force, ramped in, before the cap.
  double end_stop() const { return end_stop_; }
  // The condition effects for this tick: zero while blocked or stale, ramped in, scaled by the
  // gain, inside the cap together with the constant force.
  const Condition& spring() const { return spring_; }
  const Condition& damper() const { return damper_; }
  // The periodic effects' magnitudes for this tick, inside the cap with the rest: the road's
  // vibration, and the impact that plays (zero once its length has passed). impacts() counts
  // the impacts started, so a sink starts a burst when it changes; impact_left_s() is what is
  // left of its length.
  double road() const { return road_; }
  double impact() const { return impact_; }
  uint64_t impacts() const { return impacts_; }
  double impact_left_s(double now_s) const {
    return impact_ > 0.0 ? std::max(0.0, impact_length_s_ - (now_s - impact_start_s_)) : 0.0;
  }
  uint32_t flags() const { return flags_; }

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
    config.min_force = std::clamp(finite_or(config.min_force, 0.0), 0.0, 0.5);
    config.smoothing_s = std::clamp(finite_or(config.smoothing_s, 0.0), 0.0, 0.2);
    config.condition_share = std::clamp(finite_or(config.condition_share, 0.5), 0.0, 1.0);
    config.effect_share = std::clamp(finite_or(config.effect_share, 0.25), 0.0,
                                     1.0 - config.condition_share);
    return config;
  }

 private:
  static constexpr double kMinForceKnee = 0.02;
  // The smallest change of a condition's level that is passed on.
  static constexpr double kConditionStep = 0.002;

  Condition CleanCondition(const Condition& condition) {
    const auto clean = [this](double value) {
      if (!std::isfinite(value) || value < 0.0 || value > 1.0) {
        bad_value_ = true;
        return 0.0;
      }
      return value;
    };
    return {clean(condition.coefficient), clean(condition.saturation)};
  }

  double CleanLevel(double level) {
    if (!std::isfinite(level) || level < 0.0 || level > 1.0) {
      bad_value_ = true;
      return 0.0;
    }
    return level;
  }

  void StopImpact() {
    impact_ = 0.0;
    impact_pending_ = false;
  }

  // A periodic effect's next magnitude, as Hold moves a condition's saturation.
  double HoldLevel(double current, double wanted, double& last, double step) {
    const bool still = wanted == last;
    last = wanted;
    const double next = Slew(current, wanted, step);
    if (next == 0.0) {
      return 0.0;
    }
    return still || std::fabs(next - current) >= kConditionStep ? next : current;
  }

  Condition Scaled(const Condition& condition) const {
    return {std::clamp(condition.coefficient * config_.gain, 0.0, 1.0) * envelope_,
            std::clamp(condition.saturation * config_.gain, 0.0, 1.0) * envelope_};
  }

  double MinForce(double force) const {
    const double m = config_.min_force;
    const double magnitude = std::fabs(force);
    if (m <= 0.0 || magnitude == 0.0) {
      return force;
    }
    const double lifted = magnitude >= kMinForceKnee
                              ? m + (1.0 - m) * magnitude
                              : magnitude / kMinForceKnee * (m + (1.0 - m) * kMinForceKnee);
    return std::copysign(lifted, force);
  }

  // The condition's next level: towards `wanted` at the slew limit; while `wanted` moves, only by
  // a step of at least kConditionStep, and exactly once it holds still (`last` is the last tick's
  // wanted level).
  Condition Hold(const Condition& current, const Condition& wanted, Condition& last, double step) {
    const bool still = wanted.coefficient == last.coefficient &&
                       wanted.saturation == last.saturation;
    last = wanted;
    const Condition next{wanted.coefficient, Slew(current.saturation, wanted.saturation, step)};
    if (next.coefficient == 0.0 || next.saturation == 0.0) {
      return {};
    }
    if (still || std::fabs(next.coefficient - current.coefficient) >= kConditionStep ||
        std::fabs(next.saturation - current.saturation) >= kConditionStep) {
      return next;
    }
    return current;
  }

  double Slew(double from, double to, double step) {
    const double delta = to - from;
    if (std::fabs(delta) > step) {
      flags_ |= kFlagSlewLimited;
      return from + std::copysign(step, delta);
    }
    return to;
  }

  GovernorConfig config_;
  double target_ = 0.0;
  double filtered_ = 0.0;
  Condition spring_target_;
  Condition damper_target_;
  double output_ = 0.0;
  Condition spring_;
  Condition damper_;
  Condition spring_wanted_;
  Condition damper_wanted_;
  double end_stop_target_ = 0.0;
  double end_stop_ = 0.0;
  double road_target_ = 0.0;
  double road_ = 0.0;
  double road_wanted_ = 0.0;
  double impact_wanted_ = 0.0;
  double impact_length_s_ = 0.0;
  double impact_start_s_ = 0.0;
  bool impact_pending_ = false;
  double impact_ = 0.0;
  uint64_t impacts_ = 0;
  double envelope_ = 0.0;
  double last_submit_s_ = 0.0;
  double last_tick_s_ = 0.0;
  bool has_submit_ = false;
  bool has_tick_ = false;
  bool bad_value_ = false;
  uint32_t blocked_ = kBlockNone;
  uint32_t flags_ = 0;
};

// The soft lock's end stop (stage 5): beyond the car's lock, a force back towards it, sent as part
// of the constant force, so the wheel stops turning where the car's steering does. It rises from
// zero over the first knee_deg past the lock as a parabola (so its slope starts at zero too),
// grows linearly to its strength travel_deg past the lock, and holds it beyond. Inside the stop a
// damper on the wheel's speed takes the bounce out of it: it adds to the push back while the wheel
// turns outwards and takes from it while the wheel returns, but never pushes outwards, and it
// fades in over the knee as the spring does, so the force stays continuous at the lock.
struct EndStopConfig {
  // The force travel_deg past the lock, a fraction of full scale before the cap; 0: off.
  double strength = 1.0;
  double travel_deg = 6.0;
  double knee_deg = 2.0;
  // The damper: at a speed of v degrees a second outwards it adds what the spring adds over
  // v x damping_s degrees.
  double damping_s = 0.02;
  // The time constant of the low-pass on the wheel's speed.
  double speed_smoothing_s = 0.01;
};

class EndStop {
 public:
  explicit EndStop(const EndStopConfig& config = {}) : config_(Sanitize(config)) {}

  void Configure(const EndStopConfig& config) { config_ = Sanitize(config); }
  const EndStopConfig& config() const { return config_; }

  // The force for the wheel's angle (degrees from the centre, right positive) and the car's lock
  // (lock to lock, degrees); positive is to the right, as the title's force.
  double Step(double angle_deg, double lock_deg, double now_s) {
    if (!std::isfinite(angle_deg) || !std::isfinite(now_s)) {
      Reset();
      return 0.0;
    }
    const double dt = has_angle_ ? now_s - last_s_ : 0.0;
    if (has_angle_ && dt > 0.0 && dt <= 0.1) {
      const double speed = (angle_deg - last_angle_) / dt;
      speed_ += (speed - speed_) * (1.0 - std::exp(-dt / config_.speed_smoothing_s));
    } else if (!has_angle_ || dt > 0.1 || dt < 0.0) {
      speed_ = 0.0;
    }
    if (!has_angle_ || dt != 0.0) {
      last_angle_ = angle_deg;
      last_s_ = now_s;
      has_angle_ = true;
    }
    force_ = 0.0;
    if (config_.strength <= 0.0 || !std::isfinite(lock_deg) || lock_deg <= 0.0) {
      return force_;
    }
    const double past = std::fabs(angle_deg) - lock_deg * 0.5;
    if (past <= 0.0) {
      return force_;
    }
    const double knee = config_.knee_deg, travel = config_.travel_deg;
    const double span = travel - knee * 0.5;
    const double spring = past < knee     ? past * past / (2.0 * knee * span)
                          : past < travel ? (past - knee * 0.5) / span
                                          : 1.0;
    const double outwards = std::copysign(1.0, angle_deg) * speed_;
    const double damper = std::min(1.0, past / knee) * outwards * config_.damping_s / span;
    const double push = std::clamp(spring + damper, 0.0, 1.0) * config_.strength;
    force_ = -std::copysign(push, angle_deg);
    return force_;
  }

  void Reset() {
    has_angle_ = false;
    speed_ = 0.0;
    force_ = 0.0;
  }

  double force() const { return force_; }
  // The wheel's smoothed speed, degrees a second.
  double speed() const { return speed_; }

  static EndStopConfig Sanitize(EndStopConfig config) {
    const auto finite_or = [](double value, double fallback) {
      return std::isfinite(value) ? value : fallback;
    };
    config.strength = std::clamp(finite_or(config.strength, 0.0), 0.0, 1.0);
    config.knee_deg = std::clamp(finite_or(config.knee_deg, 2.0), 0.1, 45.0);
    config.travel_deg = std::clamp(finite_or(config.travel_deg, 6.0), config.knee_deg, 90.0);
    config.damping_s = std::clamp(finite_or(config.damping_s, 0.02), 0.0, 0.5);
    config.speed_smoothing_s = std::clamp(finite_or(config.speed_smoothing_s, 0.01), 0.001, 0.2);
    return config;
  }

 private:
  EndStopConfig config_;
  double last_angle_ = 0.0;
  double last_s_ = 0.0;
  bool has_angle_ = false;
  double speed_ = 0.0;
  double force_ = 0.0;
};

}  // namespace rex::input::wheel
