// CPU tests of the racing wheel's force feedback safety governor
// (rex/input/wheel/ffb_governor.h, docs/analysis/racing-wheels.md) on
// synthetic force streams: the ramp-in, the cap independent of the gain, NaN,
// infinities and out-of-range values, spikes against the slew limit, focus
// loss and the other zero states, the watchdog, a stalled force thread and the
// condition effects' level. Returns 1 on failure.

#include <cmath>
#include <cstdio>
#include <limits>

#include <rex/input/wheel/ffb_governor.h>

namespace {

using rex::input::wheel::Governor;
using rex::input::wheel::GovernorConfig;
namespace wheel = rex::input::wheel;

int failures = 0;

void Check(bool condition, const char* what) {
  if (!condition) {
    std::printf("FAIL %s\n", what);
    ++failures;
  }
}

constexpr double kTick = 0.001;

// Feeds `value` every tick for `seconds` from `t` and returns the last output; `t` advances.
double Run(Governor& governor, double& t, double value, double seconds, double* max_abs = nullptr,
           double* max_step = nullptr) {
  double out = governor.output();
  const int ticks = int(std::lround(seconds / kTick));
  for (int i = 0; i < ticks; ++i) {
    t += kTick;
    governor.Submit(value, t);
    const double previous = out;
    out = governor.Step(t);
    if (max_abs) *max_abs = std::fmax(*max_abs, std::fabs(out));
    if (max_step) *max_step = std::fmax(*max_step, std::fabs(out - previous));
  }
  return out;
}

// Steps without new source values.
double Idle(Governor& governor, double& t, double seconds) {
  double out = governor.output();
  const int ticks = int(std::lround(seconds / kTick));
  for (int i = 0; i < ticks; ++i) {
    t += kTick;
    out = governor.Step(t);
  }
  return out;
}

GovernorConfig Fast() {
  GovernorConfig config;
  config.cap = 1.0;
  config.slew_per_s = 100.0;
  config.ramp_s = 1.5;
  return config;
}

}  // namespace

int main() {
  // Nothing before the first value: zero, and the watchdog flag.
  {
    Governor governor;
    double t = 0.0;
    Check(Idle(governor, t, 0.2) == 0.0, "no source gives zero");
    Check((governor.flags() & wheel::kFlagWatchdog) != 0, "no source sets the watchdog flag");
  }

  // The ramp-in: linear over ramp_s, monotonic, full at its end.
  {
    Governor governor(Fast());
    double t = 0.0;
    const double half = Run(governor, t, 1.0, 0.75);
    Check(std::fabs(half - 0.5) < 0.01, "ramp is half way at half the ramp time");
    Check((governor.flags() & wheel::kFlagRamping) != 0, "ramping flag during the ramp");
    double max_step = 0.0;
    const double full = Run(governor, t, 1.0, 0.8, nullptr, &max_step);
    Check(std::fabs(full - 1.0) < 1e-9, "ramp reaches the full value");
    Check((governor.flags() & wheel::kFlagRamping) == 0, "no ramping flag after the ramp");
    Check(max_step <= kTick / 1.5 + 1e-9, "ramp steps stay at the ramp's rate");
  }

  // The cap holds whatever the gain and the source.
  {
    GovernorConfig config = Fast();
    config.cap = 0.25;
    config.gain = 4.0;
    config.ramp_s = 0.0;
    Governor governor(config);
    double t = 0.0;
    double max_abs = 0.0;
    Run(governor, t, 1.0, 0.5, &max_abs);
    Run(governor, t, -1.0, 0.5, &max_abs);
    Check(max_abs <= 0.25 + 1e-12, "the cap holds at gain 4");
    Check(std::fabs(governor.output() + 0.25) < 1e-12, "negative forces reach the cap too");
    Check((governor.flags() & wheel::kFlagCapped) != 0, "capped flag");
  }

  // NaN, infinities and out-of-range values become zero, at the slew limit.
  {
    GovernorConfig config = Fast();
    config.ramp_s = 0.0;
    config.slew_per_s = 4.0;
    Governor governor(config);
    double t = 0.0;
    Run(governor, t, 0.2, 0.2);
    Check(std::fabs(governor.output() - 0.2) < 1e-9, "steady value before the bad values");
    const double bad[] = {std::numeric_limits<double>::quiet_NaN(),
                          std::numeric_limits<double>::infinity(),
                          -std::numeric_limits<double>::infinity(), 1.5, -7.0, 1e300};
    for (double value : bad) {
      Governor copy = governor;
      double tc = t;
      double max_abs = 0.0;
      const double out = Run(copy, tc, value, 0.2, &max_abs);
      Check(out == 0.0, "a bad value ends at zero");
      Check(max_abs <= 0.2 + 1e-9, "a bad value never raises the force");
      Check(std::isfinite(out), "the output stays finite");
      Check((copy.flags() & wheel::kFlagBadValue) != 0, "bad value flag");
    }
  }

  // A single-tick spike is clamped by the slew limit.
  {
    GovernorConfig config;
    config.cap = 1.0;
    config.ramp_s = 0.0;
    config.slew_per_s = 4.0;
    Governor governor(config);
    double t = 0.0;
    Run(governor, t, 0.0, 0.1);
    t += kTick;
    governor.Submit(1.0, t);
    const double spike = governor.Step(t);
    Check(spike <= 4.0 * kTick + 1e-12, "a full-scale spike moves one slew step");
    Check((governor.flags() & wheel::kFlagSlewLimited) != 0, "slew flag on the spike");
    double max_abs = 0.0;
    Run(governor, t, 0.0, 0.05, &max_abs);
    Check(max_abs <= 4.0 * kTick + 1e-12, "the spike never builds up");
  }

  // Focus loss, pause, exit, no device and the user's stop: zero at once, then a new ramp.
  {
    for (uint32_t reason : {wheel::kBlockFocus, wheel::kBlockPaused, wheel::kBlockExit,
                            wheel::kBlockNoDevice, wheel::kBlockUser}) {
      Governor governor(Fast());
      double t = 0.0;
      Run(governor, t, 0.8, 2.0);
      Check(std::fabs(governor.output() - 0.8) < 1e-9, "steady before the block");
      governor.SetBlocked(reason, true);
      t += kTick;
      governor.Submit(0.8, t);
      Check(governor.Step(t) == 0.0, "a block zeroes at once");
      Check(governor.ConditionLevel(1.0) == 0.0, "a block zeroes the conditions");
      Check((governor.flags() & wheel::kFlagBlocked) != 0, "blocked flag");
      Run(governor, t, 0.8, 0.5);
      Check(governor.output() == 0.0, "zero for as long as the block holds");
      governor.SetBlocked(reason, false);
      const double after = Run(governor, t, 0.8, 0.01);
      Check(after > 0.0 && after < 0.8 * 0.011 / 1.5 + 1e-9, "the force ramps in again after the block");
    }
    // Two reasons: both must clear.
    Governor governor(Fast());
    double t = 0.0;
    governor.SetBlocked(wheel::kBlockFocus, true);
    governor.SetBlocked(wheel::kBlockPaused, true);
    governor.SetBlocked(wheel::kBlockFocus, false);
    Check(Run(governor, t, 0.5, 0.5) == 0.0, "one remaining reason keeps the force at zero");
  }

  // The watchdog: no new value for watchdog_s and the force falls to zero, then ramps in again.
  {
    GovernorConfig config = Fast();
    config.ramp_s = 0.0;
    config.slew_per_s = 10.0;
    config.watchdog_s = 0.05;
    Governor governor(config);
    double t = 0.0;
    Run(governor, t, 0.5, 0.2);
    Check(std::fabs(Idle(governor, t, 0.04) - 0.5) < 1e-9, "the force holds within the watchdog time");
    Idle(governor, t, 0.02);
    Check((governor.flags() & wheel::kFlagWatchdog) != 0, "watchdog flag after its time");
    Check(governor.output() < 0.5, "the force falls after the watchdog time");
    Check(Idle(governor, t, 0.06) == 0.0, "the force reaches zero at the slew limit");
    config.ramp_s = 1.0;
    governor.Configure(config);
    const double after = Run(governor, t, 0.5, 0.01);
    Check(after > 0.0 && after <= 0.5 * 0.011 + 1e-9, "values after the watchdog ramp in again");
  }

  // A stalled force thread: the slew limit counts at most max_tick_s.
  {
    GovernorConfig config;
    config.cap = 1.0;
    config.ramp_s = 0.0;
    config.slew_per_s = 4.0;
    config.max_tick_s = 0.01;
    Governor governor(config);
    double t = 0.0;
    Run(governor, t, 0.0, 0.05);
    t += 1.0;
    governor.Submit(1.0, t);
    Check(governor.Step(t) <= 4.0 * 0.01 + 1e-12, "a one-second stall moves one bounded step");
  }

  // Condition effects: capped, scaled by the ramp, zero without a fresh source.
  {
    GovernorConfig config = Fast();
    config.cap = 0.3;
    config.ramp_s = 1.0;
    Governor governor(config);
    double t = 0.0;
    Check(governor.ConditionLevel(0.25) == 0.0, "no condition before the ramp");
    Run(governor, t, 0.0, 0.5);
    Check(std::fabs(governor.ConditionLevel(0.25) - 0.125) < 0.002, "the condition ramps in");
    Run(governor, t, 0.0, 0.6);
    Check(std::fabs(governor.ConditionLevel(0.25) - 0.25) < 1e-9, "the condition reaches its level");
    Check(std::fabs(governor.ConditionLevel(0.9) - 0.3) < 1e-9, "the condition is capped");
    Check(governor.ConditionLevel(-0.5) == 0.0, "a negative condition is zero");
    Check(governor.ConditionLevel(std::numeric_limits<double>::quiet_NaN()) == 0.0,
          "a NaN condition is zero");
    Idle(governor, t, 0.1);
    Check(governor.ConditionLevel(0.25) == 0.0, "no condition after the watchdog");
  }

  // The configuration is sanitized.
  {
    GovernorConfig config;
    config.cap = std::numeric_limits<double>::quiet_NaN();
    config.gain = -2.0;
    config.slew_per_s = 0.0;
    config.ramp_s = std::numeric_limits<double>::infinity();
    const GovernorConfig clean = Governor::Sanitize(config);
    Check(clean.cap == 0.0, "a NaN cap becomes zero");
    Check(clean.gain == 0.0, "a negative gain becomes zero");
    Check(clean.slew_per_s >= 0.1, "the slew limit has a floor");
    Check(clean.ramp_s == 1.5, "an infinite ramp takes the default");
    config = {};
    config.cap = 3.0;
    Check(Governor::Sanitize(config).cap == 1.0, "the cap is at most full scale");
  }

  std::printf("%s\n", failures ? "wheel governor tests FAILED" : "wheel governor tests passed");
  return failures ? 1 : 0;
}
