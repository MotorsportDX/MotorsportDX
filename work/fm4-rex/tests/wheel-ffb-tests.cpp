// CPU tests of the racing wheel's force feedback safety governor
// (rex/input/wheel/ffb_governor.h, docs/analysis/racing-wheels.md) on
// synthetic force streams: the ramp-in, the cap independent of the gain, NaN,
// infinities and out-of-range values, spikes against the slew limit, focus
// loss and the other zero states, the watchdog, a stalled force thread, the
// condition effects inside the cap together with the constant force, the
// periodic effects (the road's vibration and impacts) inside their share of
// it, the minimum force, the smoothing, and the playback of a force that
// arrives in bursts at the physics rate (ffb_stream.h). Returns 1 on failure.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

#include <rex/input/wheel/ffb_governor.h>
#include <rex/input/wheel/ffb_stream.h>

namespace {

using rex::input::wheel::Condition;
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
      governor.Submit(0.8, t, {0.5, 0.5}, {0.1, 0.1});
      Check(governor.Step(t) == 0.0, "a block zeroes at once");
      Check(governor.spring().saturation == 0.0 && governor.damper().saturation == 0.0,
            "a block zeroes the conditions");
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

  // Condition effects: scaled by the ramp and the gain, zero without a fresh source.
  {
    GovernorConfig config = Fast();
    config.cap = 1.0;
    config.ramp_s = 1.0;
    Governor governor(config);
    double t = 0.0;
    const Condition spring{0.4, 0.25};
    const auto feed = [&](double seconds) {
      for (int i = 0; i < int(std::lround(seconds / kTick)); ++i) {
        t += kTick;
        governor.Submit(0.0, t, spring);
        governor.Step(t);
      }
    };
    Check(governor.spring().saturation == 0.0, "no condition before the ramp");
    feed(0.5);
    Check(std::fabs(governor.spring().saturation - 0.125) < 0.003, "the condition ramps in");
    Check(std::fabs(governor.spring().coefficient - 0.2) < 0.003, "its coefficient ramps in");
    feed(0.6);
    Check(std::fabs(governor.spring().saturation - 0.25) < 1e-9, "the condition reaches its level");
    config.gain = 2.0;
    governor.Configure(config);
    feed(0.1);
    Check(std::fabs(governor.spring().saturation - 0.5) < 1e-9, "the gain scales the condition");
    Idle(governor, t, 0.1);
    Check(governor.spring().saturation == 0.0, "no condition after the watchdog");
    for (const Condition bad : {Condition{-0.5, 0.5}, Condition{0.5, 1.5},
                                Condition{std::numeric_limits<double>::quiet_NaN(), 0.5}}) {
      Governor copy(Fast());
      double tc = 0.0;
      for (int i = 0; i < 2000; ++i) {
        tc += kTick;
        copy.Submit(0.0, tc, bad);
        copy.Step(tc);
      }
      Check(copy.spring().coefficient == 0.0 || copy.spring().saturation == 0.0,
            "a bad condition is zero");
      Check((copy.flags() & wheel::kFlagBadValue) != 0, "a bad condition sets the flag");
    }
  }

  // The cap bounds the constant force and the conditions together; the conditions take at most
  // half of it.
  {
    GovernorConfig config = Fast();
    config.cap = 0.25;
    config.ramp_s = 0.0;
    config.slew_per_s = 4.0;
    Governor governor(config);
    double t = 0.0;
    double worst = 0.0;
    const auto feed = [&](double force, Condition spring, Condition damper, double seconds) {
      for (int i = 0; i < int(std::lround(seconds / kTick)); ++i) {
        t += kTick;
        governor.Submit(force, t, spring, damper);
        const double out = governor.Step(t);
        worst = std::fmax(worst, std::fabs(out) + governor.spring().saturation +
                                     governor.damper().saturation);
      }
    };
    // The title's mix at full scale: a force of 1, its spring at 0.5 and its damper at 0.1.
    feed(1.0, {0.5, 0.5}, {0.1, 0.1}, 1.0);
    Check(worst <= 0.25 + 1e-9, "the sum stays within the cap");
    Check(std::fabs(governor.spring().saturation - 0.125 * 0.5 / 0.6) < 1e-9 &&
              std::fabs(governor.damper().saturation - 0.125 * 0.1 / 0.6) < 1e-9,
          "the conditions share half the cap in their proportion");
    Check(std::fabs(governor.output() - 0.125) < 1e-9, "the force gets the rest");
    Check((governor.flags() & wheel::kFlagCapped) != 0, "capped flag for the sum");
    // Changes of a condition's level are passed on in real steps only.
    int changes = 0;
    Condition last = governor.spring();
    for (int i = 0; i < 1000; ++i) {
      t += kTick;
      governor.Submit(0.2, t, {0.1 + 0.00001 * i, 0.1 + 0.00001 * i});
      governor.Step(t);
      const Condition now = governor.spring();
      if (now.saturation != last.saturation || now.coefficient != last.coefficient) {
        ++changes;
        Check(std::fabs(now.saturation - last.saturation) >= 0.002 - 1e-12 ||
                  std::fabs(now.coefficient - last.coefficient) >= 0.002 - 1e-12,
              "a moving condition changes in real steps");
      }
      last = now;
    }
    Check(changes > 2 && changes < 60, "a slowly moving condition sends few updates");
    // The spring grows while the force is at its share: the force slews down and the spring
    // gives way meanwhile, so the sum never exceeds the cap.
    feed(0.1, {0.0, 0.0}, {0.0, 0.0}, 0.5);
    feed(1.0, {1.0, 1.0}, {0.0, 0.0}, 0.5);
    feed(-1.0, {1.0, 1.0}, {0.1, 0.1}, 0.5);
    Check(worst <= 0.25 + 1e-9, "the sum stays within the cap while the mix changes");
    // Below the cap nothing is scaled.
    GovernorConfig open = config;
    open.cap = 1.0;
    governor.Configure(open);
    feed(0.3, {0.2, 0.2}, {0.1, 0.1}, 1.0);
    Check(std::fabs(governor.output() - 0.3) < 1e-9 &&
              std::fabs(governor.spring().saturation - 0.2) < 1e-9,
          "below the cap the mix is unchanged");
  }

  // The periodic effects: within their share of the cap, with the constant force and the
  // conditions in the same sum; an impact for its length, the road's vibration in what it leaves.
  {
    GovernorConfig config = Fast();
    config.cap = 0.25;
    config.ramp_s = 0.0;
    config.slew_per_s = 4.0;
    Governor governor(config);
    double t = 0.0;
    double worst = 0.0;
    uint64_t impacts = 0;
    const auto feed = [&](double force, double road, double seconds) {
      for (int i = 0; i < int(std::lround(seconds / kTick)); ++i) {
        t += kTick;
        governor.Submit(force, t, {0.5, 0.5}, {0.1, 0.1});
        governor.SubmitRoad(road);
        const double out = governor.Step(t);
        worst = std::fmax(worst, std::fabs(out) + governor.spring().saturation +
                                     governor.damper().saturation + governor.road() +
                                     governor.impact());
      }
    };
    feed(1.0, 0.0, 0.5);
    Check(governor.road() == 0.0 && governor.impact() == 0.0, "no effects without a source");
    // A kerb: the road's vibration rises at the slew limit to its share of the cap.
    feed(1.0, 0.8, 0.005);
    Check(governor.road() > 0.0 && governor.road() <= 4.0 * 0.005 + 1e-9,
          "the road's vibration rises at the slew limit");
    feed(1.0, 0.8, 0.5);
    Check(std::fabs(governor.road() - 0.25 * 0.25) < 1e-9, "the road's vibration takes its share");
    Check(std::fabs(governor.output() - (0.25 - 0.125 - 0.0625)) < 1e-9,
          "the constant force gets what the conditions and effects leave");
    // An impact during the kerb: its own magnitude at once, the road's vibration gives way.
    governor.Impact(0.04, 0.2, t);
    feed(1.0, 0.8, 0.001);
    Check(std::fabs(governor.impact() - 0.04) < 1e-9, "an impact starts at its level");
    Check(governor.impacts() == ++impacts, "an impact is counted");
    Check(std::fabs(governor.road() - (0.0625 - 0.04)) < 1e-9,
          "the road's vibration gives way to the impact");
    Check(std::fabs(governor.impact_left_s(t) - 0.2) < 0.002, "the impact's length is left");
    feed(1.0, 0.8, 0.25);
    Check(governor.impact() == 0.0, "an impact ends after its length");
    Check(std::fabs(governor.road() - 0.0625) < 1e-9, "the road's vibration takes its room back");
    // An impact beyond the share is cut to it, and the gain scales it.
    GovernorConfig loud = config;
    loud.gain = 2.0;
    governor.Configure(loud);
    governor.Impact(1.0, 0.1, t);
    feed(1.0, 0.0, 0.002);
    Check(std::fabs(governor.impact() - 0.0625) < 1e-9, "an impact is cut to the share");
    Check(worst <= 0.25 + 1e-9, "the sum with the effects stays within the cap");
    // Every zero state stops the effects at once.
    governor.Configure(config);
    feed(1.0, 0.8, 0.5);
    governor.Impact(0.04, 0.2, t);
    feed(1.0, 0.8, 0.001);
    governor.SetBlocked(wheel::kBlockPaused, true);
    governor.Step(t += kTick);
    Check(governor.road() == 0.0 && governor.impact() == 0.0, "a block stops the effects");
    governor.SetBlocked(wheel::kBlockPaused, false);
    feed(1.0, 0.8, 0.5);
    governor.Impact(0.04, 0.2, t);
    feed(1.0, 0.8, 0.001);
    Idle(governor, t, 0.06);
    Check(governor.road() == 0.0 && governor.impact() == 0.0, "the watchdog stops the effects");
    // An impact submitted while the source is stale does not start later.
    governor.Impact(0.04, 0.2, t);
    Idle(governor, t, 0.01);
    feed(1.0, 0.0, 0.01);
    Check(governor.impact() == 0.0, "a stale impact never starts");
    // Bad levels become zero.
    governor.SubmitRoad(std::numeric_limits<double>::quiet_NaN());
    feed(1.0, -1.0, 0.01);
    Check(governor.road() == 0.0 && (governor.flags() & wheel::kFlagBadValue) != 0,
          "a bad road level is zero");
    // The share is sanitized against the conditions'.
    GovernorConfig shares;
    shares.condition_share = 0.9;
    shares.effect_share = 0.5;
    Check(std::fabs(Governor::Sanitize(shares).effect_share - 0.1) < 1e-12,
          "the shares never exceed the cap together");
  }

  // The minimum force: lifts small forces, keeps the sign, continuous through zero.
  {
    GovernorConfig config = Fast();
    config.ramp_s = 0.0;
    config.min_force = 0.1;
    Governor governor(config);
    double t = 0.0;
    Check(std::fabs(Run(governor, t, 0.05, 0.1) - (0.1 + 0.9 * 0.05)) < 1e-9,
          "a small force is lifted");
    Check(std::fabs(Run(governor, t, -0.05, 0.1) + (0.1 + 0.9 * 0.05)) < 1e-9,
          "a negative one too");
    Check(Run(governor, t, 0.0, 0.1) == 0.0, "zero stays zero");
    const double tiny = Run(governor, t, 0.001, 0.1);
    Check(tiny > 0.0 && tiny < 0.01, "near zero the force rises linearly");
    Check(std::fabs(Run(governor, t, 1.0, 0.1) - 1.0) < 1e-9, "full scale stays full scale");
  }

  // The smoothing: a low-pass on the source's steps, without overshoot.
  {
    GovernorConfig config = Fast();
    config.ramp_s = 0.0;
    config.smoothing_s = 0.01;
    Governor governor(config);
    double t = 0.0;
    Run(governor, t, 0.0, 0.1);
    const double after = Run(governor, t, 0.5, 0.01);
    Check(std::fabs(after - 0.5 * (1.0 - std::exp(-1.0))) < 0.02, "one time constant: 63%");
    double max_abs = 0.0;
    Run(governor, t, 0.5, 0.2, &max_abs);
    Check(max_abs <= 0.5 + 1e-12 && std::fabs(governor.output() - 0.5) < 1e-6,
          "the smoothing settles without overshoot");
    // A block resets it: the force starts from zero again.
    governor.SetBlocked(wheel::kBlockPaused, true);
    Idle(governor, t, 0.01);
    governor.SetBlocked(wheel::kBlockPaused, false);
    Check(Run(governor, t, 0.5, 0.001) < 0.1, "the smoothing starts over after a block");
  }

  // The physics-rate stream: a 359 Hz sine computed in bursts, one burst per frame, played back
  // on the force thread's clock. At 60, 120 and 240 frames a second the playback follows the
  // sine at one lag (about a frame) to within 0.03, a millisecond of its steepest slope; holding
  // each frame's newest step instead would be off by up to 0.42 at 60 fps and 0.1 at 240.
  for (const double fps : {60.0, 120.0, 240.0}) {
    wheel::ForceStream stream;
    constexpr double kStep = 1.0 / 359.0;
    const auto force = [](double s) { return 0.5 * std::sin(2.0 * 3.14159265358979 * 8.0 * s); };
    double sim = 0.0;
    double next_frame = 0.0;
    double delay = 0.0;
    int checked = 0;
    // The largest error against the sine at one fixed lag, for each lag: the playback follows
    // the physics when one lag fits every sample.
    std::vector<double> errors(301, 0.0);
    const double host_origin = 100.0;
    for (double now = 0.0; now < 2.0; now += kTick) {
      // A frame at its time runs the steps up to it, all at once, 1 ms after the frame starts.
      if (now >= next_frame + 0.001) {
        while (sim + kStep <= next_frame) {
          sim += kStep;
          stream.Add(sim, force(sim), host_origin + now);
        }
        next_frame += 1.0 / fps;
      }
      double value = 0.0;
      if (!stream.Sample(host_origin + now, &value) || now < 0.6) continue;
      delay = stream.delay_s();
      for (size_t lag = 0; lag < errors.size(); ++lag) {
        errors[lag] = std::fmax(errors[lag], std::fabs(value - force(now - lag * 0.0002)));
      }
      ++checked;
    }
    char what[96];
    std::snprintf(what, sizeof(what), "at %.0f fps the playback follows the physics", fps);
    Check(checked > 1000 && *std::min_element(errors.begin(), errors.end()) < 0.03, what);
    std::snprintf(what, sizeof(what), "at %.0f fps the delay is about a frame", fps);
    Check(delay > 0.5 / fps && delay < 1.0 / fps + kStep + 0.002, what);
  }
  // Late points, a rewind and an empty stream.
  {
    wheel::ForceStream stream;
    double value = 1.0;
    Check(!stream.Sample(0.0, &value), "an empty stream has no value");
    stream.Add(1.0, 0.2, 10.0);
    stream.Add(1.01, 0.4, 10.0);
    Check(stream.Sample(10.5, &value) && value == 0.4, "playback ahead holds the newest value");
    stream.Add(0.5, -0.3, 10.6);
    Check(stream.Sample(10.6, &value) && value == -0.3, "a point back in time starts over");
    stream.Add(0.6, std::numeric_limits<double>::quiet_NaN(), 10.7);
    Check(stream.Sample(10.7, &value) && value == -0.3, "a bad point is dropped");
    Check(stream.last_arrival_s() == 10.6, "the newest arrival is kept for the watchdog");
    // A hitch: a burst far later than the window allows is held, then played.
    wheel::ForceStream hitch;
    for (int i = 0; i < 200; ++i) hitch.Add(i * 0.005, 0.1, 1.0 + i * 0.005);
    for (int i = 200; i < 240; ++i) hitch.Add(i * 0.005, 0.2, 2.2);
    hitch.Sample(2.2, &value);
    Check(hitch.delay_s() <= 0.05 + 1e-12, "the delay never exceeds its limit");
  }

  // The configuration is sanitized.
  {
    GovernorConfig config;
    config.cap = std::numeric_limits<double>::quiet_NaN();
    config.gain = -2.0;
    config.slew_per_s = 0.0;
    config.ramp_s = std::numeric_limits<double>::infinity();
    config.min_force = 2.0;
    config.smoothing_s = -1.0;
    const GovernorConfig clean = Governor::Sanitize(config);
    Check(clean.min_force == 0.5, "the minimum force is at most half scale");
    Check(clean.smoothing_s == 0.0, "a negative smoothing is none");
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
