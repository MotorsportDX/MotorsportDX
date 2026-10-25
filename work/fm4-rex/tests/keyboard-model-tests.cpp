// CPU tests of the keyboard's steering and pedal model (rex/input/keyboard_model.h,
// docs/analysis/keyboard-controls.md): attack, release and reversal rates, last-pressed-wins
// steering, the same values at every poll rate, the mapping into the title's deadzones fed
// through the title's own conversion (with the profile's deadzones, a lowered one and XInput's
// pre-profile constants), the binding rule for the precision modifier, and the whole-stick
// merge of two devices (rex/input/state_merge.h). Returns 1 on failure.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string_view>
#include <vector>

#include <rex/input/keyboard_model.h>
#include <rex/input/state_merge.h>

namespace {

namespace kb = rex::input::keyboard;

int failures = 0;

void Check(bool condition, const char* what) {
  if (!condition) {
    std::printf("FAIL %s\n", what);
    ++failures;
  }
}

bool Near(double a, double b, double tolerance) {
  return std::fabs(a - b) <= tolerance;
}

// The title's steering from a stick (sub_822B3F18): a radial deadzone, rescaled
// linearly, each axis clamped (docs/analysis/modern-gamepads.md, measured in the e2e sweep).
double TitleSteering(int16_t x, int16_t y, const kb::TitleRange& range) {
  const double m = std::sqrt(double(x) * x + double(y) * y);
  if (m <= range.inner) {
    return 0.0;
  }
  const double scaled = double(x) * (m - range.inner) / m / (range.outer - range.inner);
  return std::fmax(-1.0, std::fmin(1.0, scaled));
}

// The title's trigger (sub_82394A60): 0 up to inner, then (t - inner) / (outer - inner),
// clamped.
double TitleTrigger(uint8_t t, const kb::TitleRange& range) {
  if (t <= range.inner) {
    return 0.0;
  }
  return std::fmin(1.0, (t - range.inner) / (range.outer - range.inner));
}

// The profile's defaults (ForzaProfile.sch) in raw units, as the title passes them.
const kb::TitleRange kSteering{0.24 * 32767.0, 0.95 * 32767.0};
const kb::TitleRange kSteeringLowered{0.05 * 32767.0, 0.95 * 32767.0};
const kb::TitleRange kSteeringXInput{8689.0, 32767.0};
const kb::TitleRange kThrottle{0.15 * 255.0, 0.90 * 255.0};
const kb::TitleRange kBrake{0.15 * 255.0, 0.95 * 255.0};
const kb::TitleRange kTriggerXInput{30.0, 254.0};

// A key script: the target from 'time' on.
struct Change {
  double time;
  double target;
};

// The value at 'end' with polls every 'period' seconds (and, with 'double_poll', a second poll
// 1 ms later), the driver's way: at each key change and each poll it advances to that time.
double Simulate(const std::vector<Change>& script, double end, double period, bool double_poll,
                const kb::AxisRates& rates, std::vector<std::pair<double, double>>* samples) {
  std::vector<double> times;
  for (double t = 0.0; t <= end + 1e-12; t += period) {
    times.push_back(t);
    if (double_poll) times.push_back(t + 0.001);
  }
  double value = 0.0;
  double now = 0.0;
  double target = 0.0;
  size_t next_change = 0;
  for (double poll : times) {
    while (next_change < script.size() && script[next_change].time <= poll) {
      value = kb::AdvanceAxis(value, target, script[next_change].time - now, rates);
      now = script[next_change].time;
      target = script[next_change].target;
      ++next_change;
    }
    value = kb::AdvanceAxis(value, target, poll - now, rates);
    now = poll;
    if (samples) samples->push_back({poll, value});
  }
  return value;
}

// The value at time t of one exact advance per change, for comparison.
double Reference(const std::vector<Change>& script, double t, const kb::AxisRates& rates) {
  double value = 0.0;
  double now = 0.0;
  double target = 0.0;
  for (const Change& change : script) {
    if (change.time > t) break;
    value = kb::AdvanceAxis(value, target, change.time - now, rates);
    now = change.time;
    target = change.target;
  }
  return kb::AdvanceAxis(value, target, t - now, rates);
}

uint16_t TestKey(std::string_view name) {
  if (name.size() == 1 && name[0] >= 'A' && name[0] <= 'Z') return uint16_t(name[0]);
  if (name == "Shift") return kb::kVkShift;
  if (name == "Control") return kb::kVkControl;
  if (name == "Up") return 0x26;
  if (name == "Space") return 0x20;
  return 0;
}

}  // namespace

int main() {
  const kb::AxisRates steering{0.30, 0.20};

  // Attack, release, and the digital behaviour at 0.
  {
    Check(Near(kb::AdvanceAxis(0.0, 1.0, 0.15, steering), 0.5, 1e-12), "half the attack time");
    Check(kb::AdvanceAxis(0.0, 1.0, 0.30, steering) == 1.0, "full after the attack time");
    Check(kb::AdvanceAxis(0.0, 1.0, 5.0, steering) == 1.0, "no overshoot");
    Check(Near(kb::AdvanceAxis(1.0, 0.0, 0.10, steering), 0.5, 1e-12), "release rate");
    Check(Near(kb::AdvanceAxis(-0.4, 0.0, 0.04, steering), -0.2, 1e-12), "release on the left");
    Check(Near(kb::AdvanceAxis(1.0, 0.5, 0.05, steering), 0.75, 1e-12),
          "toward a smaller target on the same side at the release rate");
    Check(Near(kb::AdvanceAxis(0.5, 1.0, 0.03, steering), 0.6, 1e-12), "attack from 0.5");
    const kb::AxisRates digital{0.0, 0.0};
    Check(kb::AdvanceAxis(0.0, -1.0, 0.0, digital) == -1.0, "attack 0 is at once");
    Check(kb::AdvanceAxis(-1.0, 1.0, 0.0, digital) == 1.0, "reversal at once");
    Check(kb::AdvanceAxis(0.3, 0.0, 0.0, digital) == 0.0, "release 0 is at once");
    Check(kb::AdvanceAxis(0.3, 1.0, 0.0, steering) == 0.3, "no time, no motion");
  }

  // Reversal: back to 0 at the faster rate (release, 0.20 s), then on at the attack rate.
  {
    // From full right: 0.20 s back to 0, then 0.06 s of attack to -0.2.
    Check(Near(kb::AdvanceAxis(1.0, -1.0, 0.26, steering), -0.2, 1e-12), "reversal");
    Check(Near(kb::AdvanceAxis(1.0, -1.0, 0.10, steering), 0.5, 1e-12),
          "reversal still on the right side");
    // With a slower release the attack rate is the faster one.
    const kb::AxisRates slow_release{0.10, 0.50};
    Check(Near(kb::AdvanceAxis(1.0, -1.0, 0.10, slow_release), 0.0, 1e-12),
          "reversal at the faster of attack and release");
    // Split anywhere, the same value (the path is piecewise linear).
    double split = 1.0;
    for (int i = 0; i < 26; ++i) split = kb::AdvanceAxis(split, -1.0, 0.01, steering);
    Check(Near(split, -0.2, 1e-9), "reversal in 26 steps");
  }

  // Last pressed wins with both steering keys held.
  {
    kb::SteeringKeys keys;
    Check(keys.Update(false, false) == 0.0, "no key");
    Check(keys.Update(true, false) == -1.0, "left");
    Check(keys.Update(true, true) == 1.0, "right pressed last");
    Check(keys.Update(false, true) == 1.0, "left released");
    Check(keys.Update(true, true) == -1.0, "left pressed again, last");
    Check(keys.Update(false, false) == 0.0, "both released");
  }

  // The same values at 60, 120 and 240 polls a second, with a double poll (the countdown), and
  // for a tap shorter than a frame.
  {
    const std::vector<Change> script = {
        {0.010, 1.0}, {0.400, 0.0}, {0.520, -1.0}, {0.700, 1.0}, {1.050, 0.5}, {1.300, 0.0},
        {1.5000, 1.0}, {1.5070, 0.0}};
    const kb::AxisRates rates{0.30, 0.20};
    for (double rate : {60.0, 120.0, 240.0}) {
      for (bool double_poll : {false, true}) {
        std::vector<std::pair<double, double>> samples;
        Simulate(script, 1.8, 1.0 / rate, double_poll, rates, &samples);
        bool same = true;
        for (const auto& [time, value] : samples) {
          same = same && Near(value, Reference(script, time, rates), 1e-9);
        }
        Check(same, "the value at every poll equals the exact path");
      }
    }
    // The 7 ms tap from 1.5 s: up by 0.007 / 0.30, then down at the release rate.
    const double after = Reference(script, 1.5070, rates);
    Check(Near(after, 0.007 / 0.30, 1e-12), "a tap shorter than a frame counts");
    std::vector<std::pair<double, double>> samples60;
    Simulate(script, 1.52, 1.0 / 60.0, false, rates, &samples60);
    const double at = samples60.back().first;
    Check(Near(samples60.back().second,
               std::fmax(0.0, after - (at - 1.5070) / 0.20), 1e-9),
          "the tap at 60 Hz");
  }

  // The steering mapped into the title's deadzone: linear from the first press, any profile.
  {
    for (const kb::TitleRange& range : {kSteering, kSteeringLowered, kSteeringXInput}) {
      const double step = 1.0 / (range.outer - range.inner);
      double previous = 0.0;
      bool linear = true;
      bool rising = true;
      for (int i = 1; i <= 1000; ++i) {
        const double s = i / 1000.0;
        for (double sign : {1.0, -1.0}) {
          const int16_t x = kb::StickFromTitle(sign * s, range);
          const double title = TitleSteering(x, 0, range);
          linear = linear && Near(title, sign * s, step * 1.01);
          if (sign > 0) {
            rising = rising && title > previous;
            previous = title;
          }
        }
      }
      Check(linear, "the title's steering equals the model's within one raw step");
      Check(rising, "the title's steering rises with every step");
      const double first = TitleSteering(kb::StickFromTitle(0.001, range), 0, range);
      Check(first > 0.0 && first < 0.002, "the first press steers, a little");
      Check(kb::StickFromTitle(0.0, range) == 0, "0 is centred");
      Check(TitleSteering(kb::StickFromTitle(1.0, range), 0, range) == 1.0, "1 is full lock");
    }
    // A fixed compensation for 0.24 would jump with a lowered deadzone: 0.21 at the first press.
    const int16_t fixed = kb::StickFromTitle(0.001, kSteering);
    Check(TitleSteering(fixed, 0, kSteeringLowered) > 0.2,
          "a compensation that assumes 0.24 jumps once the profile lowers it");
    // Without the title's ranges the identity: digital full deflection stays 32767.
    Check(kb::StickFromTitle(1.0, kb::TitleRanges{}.steering) == 32767, "identity range");
    Check(kb::StickFromTitle(-0.5, kb::TitleRanges{}.steering) == -16384, "identity half");
  }

  // The pedals mapped into the title's trigger ranges.
  {
    for (const kb::TitleRange& range : {kThrottle, kBrake, kTriggerXInput}) {
      const double step = 1.0 / (range.outer - range.inner);
      bool linear = true;
      double previous = 0.0;
      bool rising = true;
      for (int i = 1; i <= 200; ++i) {
        const double v = i / 200.0;
        const double title = TitleTrigger(kb::TriggerFromTitle(v, range), range);
        linear = linear && Near(title, v, step * 1.01);
        rising = rising && title >= previous;
        previous = title;
      }
      Check(linear, "the title's pedal equals the model's within one step");
      Check(rising, "the title's pedal never falls");
      Check(TitleTrigger(kb::TriggerFromTitle(0.0001, range), range) > 0.0,
            "the smallest pedal counts");
      Check(kb::TriggerFromTitle(0.0, range) == 0, "0 is released");
      Check(TitleTrigger(kb::TriggerFromTitle(1.0, range), range) == 1.0, "1 is full");
    }
  }

  // Bindings: exact modifiers as before, except a modifier bound as a key of its own.
  {
    const auto w = kb::ParseBind("W", TestKey);
    const auto up = kb::ParseBind("Shift+Up", TestKey);
    const auto ctrl_w = kb::ParseBind("Ctrl+W", TestKey);
    const auto precision = kb::ParseBind("Control", TestKey);
    const auto shift = kb::ParseBind("Shift", TestKey);
    bool down[256] = {};
    down['W'] = true;
    Check(kb::BindHeld(w, down, kb::LiveModifiers(down), 0), "W");
    down[kb::kVkControl] = true;
    const uint8_t live = kb::LiveModifiers(down);
    Check(!kb::BindHeld(w, down, live, 0), "Ctrl silences W without a bare Ctrl binding");
    Check(kb::BindHeld(ctrl_w, down, live, 0), "Ctrl+W without a bare Ctrl binding");
    const uint8_t ignored = kb::BareModifiers(precision);
    Check(ignored == kb::kModCtrl, "Control bound as a key");
    Check(kb::BindHeld(w, down, live, ignored), "W with the precision key held");
    Check(kb::BindHeld(precision, down, live, ignored), "the precision key itself");
    down[kb::kVkControl] = false;
    Check(!kb::BindHeld(precision, down, kb::LiveModifiers(down), ignored),
          "the precision key released");
    // Shift+Up keeps its exact match, with or without Ctrl.
    down[0x26] = true;
    down[kb::kVkShift] = true;
    Check(kb::BindHeld(up, down, kb::LiveModifiers(down), ignored), "Shift+Up");
    down[kb::kVkControl] = true;
    Check(kb::BindHeld(up, down, kb::LiveModifiers(down), ignored), "Shift+Up with Ctrl");
    down[kb::kVkShift] = false;
    Check(!kb::BindHeld(up, down, kb::LiveModifiers(down), ignored), "Up alone is not Shift+Up");
    // Today a bare modifier never fired; bound, it fires.
    down[kb::kVkShift] = true;
    Check(kb::BindHeld(shift, down, kb::LiveModifiers(down), kb::kModCtrl), "a bare Shift binding");
    Check(kb::ParseBind("Q,I", TestKey).size() == 2, "alternatives");
    Check(kb::ParseBind("Nonsense", TestKey).empty(), "an unknown name never fires");
  }

  // Ctrl held with W: the throttle's target is the precision share, 50% in the title's terms.
  {
    const auto w = kb::ParseBind("W", TestKey);
    const auto precision = kb::ParseBind("Control", TestKey);
    const uint8_t ignored = kb::BareModifiers(w) | kb::BareModifiers(precision);
    bool down[256] = {};
    down['W'] = true;
    down[kb::kVkControl] = true;
    const uint8_t live = kb::LiveModifiers(down);
    const double limit = kb::BindHeld(precision, down, live, ignored) ? 0.5 : 1.0;
    const double target = kb::BindHeld(w, down, live, ignored) ? limit : 0.0;
    const double throttle = kb::AdvanceAxis(0.0, target, 1.0, {0.25, 0.10});
    const double title = TitleTrigger(kb::TriggerFromTitle(throttle, kThrottle), kThrottle);
    Check(Near(title, 0.5, 1.0 / (kThrottle.outer - kThrottle.inner) * 1.01),
          "Ctrl and W give the title 50% throttle");
  }

  // Two devices on one user: each stick comes whole from the device deflected further, so the
  // keyboard's steering never adds to a pad's.
  {
    int16_t x = 0, y = 20000;  // a pad pushed up
    rex::input::MergeStick(x, y, kb::StickFromTitle(0.1, kSteering), 0);
    Check(x == 0 && y == 20000, "the pad deflected further keeps its whole stick");
    Check(TitleSteering(x, y, kSteering) == 0.0, "no steering from the keyboard added");
    x = 5000;
    y = 0;  // a pad inside the deadzone
    const int16_t key = kb::StickFromTitle(0.3, kSteering);
    rex::input::MergeStick(x, y, key, 0);
    Check(x == key && y == 0, "the keyboard deflected further takes the whole stick");
    Check(Near(TitleSteering(x, y, kSteering), 0.3, 1e-4), "exactly the keyboard's steering");
    x = 100;
    y = -100;
    rex::input::MergeStick(x, y, -100, 100);
    Check(x == 100 && y == -100, "a tie keeps the first device");
  }

  if (failures) {
    std::printf("%d failures\n", failures);
    return 1;
  }
  std::printf("keyboard model tests passed\n");
  return 0;
}
