// CPU tests of the physical pads' stick and trigger shaping and rumble scaling
// (rex/input/pad_shaping.h, docs/analysis/modern-gamepads.md): the identity at
// the defaults, the radial rescale, the curve, the trigger range, the clamps,
// the steering anti-deadzone fed through the title's own steering conversion
// with the live ranges the title reports, its interplay with the keyboard's
// steering in the device merge, and the derived trigger rumble. Returns 1 on
// failure.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <tuple>

#include <rex/input/keyboard_model.h>
#include <rex/input/pad_shaping.h>
#include <rex/input/state_merge.h>

namespace {

using rex::input::DeriveTriggerRumble;
using rex::input::MapIntoTitleDeadzone;
using rex::input::MergeStick;
using rex::input::ScaleMotor;
using rex::input::ShapeStick;
using rex::input::ShapeTrigger;
using rex::input::StickShaping;
using rex::input::TriggerShaping;

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

// The profile's default steering deadzones, 0.24 and 0.95 of 32767
// (ForzaProfile.sch, measured in the e2e sweep), and the inner edge the title
// keeps when the profile asks for less (XInput's 7,849, keyboard-controls.md).
constexpr double kInner = 7864.08;
constexpr double kOuter = 31128.7;
constexpr double kFloorInner = 7849.0;

// The title's steering from a stick (sub_822B3F18): (m - inner) / (outer - inner)
// along the stick's direction, clamped.
double TitleSteering(int16_t x, int16_t y, double inner = kInner, double outer = kOuter) {
  const double m = std::sqrt(double(x) * x + double(y) * y);
  if (m <= inner) {
    return 0.0;
  }
  const double scaled = double(x) * (m - inner) / m / (outer - inner);
  return std::fmax(-1.0, std::fmin(1.0, scaled));
}

// The magnitude of the title's steering vector, as the merge compares sticks.
double TitleMagnitude(int16_t x, int16_t y, double inner = kInner, double outer = kOuter) {
  const double m = std::sqrt(double(x) * x + double(y) * y);
  return m <= inner ? 0.0 : std::fmin(1.0, (m - inner) / (outer - inner));
}

void Shape(int16_t& x, int16_t& y, const StickShaping& shaping) { ShapeStick(x, y, shaping); }

}  // namespace

int main() {
  // The identity at the defaults, bit for bit.
  {
    const StickShaping stick;
    const TriggerShaping trigger;
    bool same = stick.IsIdentity() && trigger.IsIdentity();
    for (int x : {-32768, -32767, -20000, -7849, -1, 0, 1, 256, 7849, 8689, 20000, 32767}) {
      for (int y : {-32768, -1, 0, 1, 15000, 32767}) {
        int16_t sx = int16_t(x), sy = int16_t(y);
        Shape(sx, sy, stick);
        same = same && sx == x && sy == y;
      }
    }
    for (int t = 0; t < 256; ++t) {
      same = same && ShapeTrigger(uint8_t(t), trigger) == t;
    }
    for (int v : {0, 1, 4915, 11468, 19660, 65535}) {
      same = same && ScaleMotor(uint16_t(v), 1.0) == v;
    }
    Check(same, "defaults are the identity");
  }

  // Radial deadzone, rescaled from its edge.
  {
    StickShaping stick;
    stick.deadzone = 0.1;
    int16_t x = 3000, y = 0;
    Shape(x, y, stick);
    Check(x == 0 && y == 0, "inside the deadzone reads centred");
    x = 6000, y = 0;
    Shape(x, y, stick);
    const double expected = (6000.0 / 32767.0 - 0.1) / 0.9 * 32767.0;
    Check(Near(x, expected, 1.0) && y == 0, "rescaled from the deadzone's edge");
    x = 3300, y = 0;
    Shape(x, y, stick);
    Check(x > 0 && x < 40, "no jump just past the edge");
    x = 6000, y = 6000;
    Shape(x, y, stick);
    Check(x == y && x > 0, "the direction is kept");
    x = 32767, y = 0;
    Shape(x, y, stick);
    Check(x == 32767, "full deflection stays full");
    x = -32768, y = -32768;
    Shape(x, y, stick);
    Check(x == -32768 && y == -32768, "diagonal corners clamp per axis");
  }

  // Outer edge and curve.
  {
    StickShaping stick;
    stick.outer = 0.9;
    int16_t x = int16_t(0.9 * 32767), y = 0;
    Shape(x, y, stick);
    Check(x >= 32766, "the outer edge gives full deflection");
    stick = {};
    stick.curve = 2.0;
    x = 16384, y = 0;
    Shape(x, y, stick);
    Check(Near(x, 0.25 * 32767, 2.0), "curve 2 squares the deflection");
    x = -16384, y = 0;
    Shape(x, y, stick);
    Check(Near(x, -0.25 * 32767, 2.0), "the curve keeps the sign");
  }

  // The title's conversion as measured (docs/analysis/modern-gamepads.md, "Measured").
  Check(TitleSteering(7849, 0) == 0.0, "title: 7,849 is inside its deadzone");
  Check(Near(TitleSteering(8000, 0), 0.0058, 0.00005), "title: 8,000 gives 0.0058");
  Check(Near(TitleSteering(16000, 0), 0.3497, 0.00005), "title: 16,000 gives 0.3497");
  Check(Near(TitleSteering(30000, 0), 0.9515, 0.00005), "title: 30,000 gives 0.9515");
  Check(Near(TitleSteering(6000, 6000), 0.0189, 0.00005), "title: (6,000, 6,000) gives 0.0189");

  // The steering anti-deadzone maps the stick into the title's live range: the title's
  // steering equals the stick's deflection from the first movement, for the default profile
  // and for the inner edge the title keeps when the profile asks for less.
  for (const double inner : {kInner, kFloorInner}) {
    double worst = 0.0;
    bool positive = true;
    bool rising = true;
    double previous = 0.0;
    for (int x = 1; x <= 32767; x += 37) {
      int16_t sx = int16_t(x), sy = 0;
      MapIntoTitleDeadzone(sx, sy, inner, kOuter);
      const double steering = TitleSteering(sx, sy, inner, kOuter);
      worst = std::fmax(worst, std::fabs(steering - x / 32767.0));
      positive = positive && steering > 0.0;
      rising = rising && steering >= previous;
      previous = steering;
    }
    // One raw step of the title's range is 1 / 23,265 (0.00004); the rounding away from 0
    // adds at most one.
    Check(worst < 0.0001, "anti-deadzone: the title's steering equals the stick's deflection");
    Check(positive, "anti-deadzone: the smallest movement steers");
    Check(rising, "anti-deadzone: the steering rises with the stick");
    int16_t sx = 32767, sy = 0;
    MapIntoTitleDeadzone(sx, sy, inner, kOuter);
    Check(TitleSteering(sx, sy, inner, kOuter) == 1.0, "anti-deadzone: full deflection is full lock");
  }
  {
    int16_t sx = 0, sy = 0;
    MapIntoTitleDeadzone(sx, sy, kInner, kOuter);
    Check(sx == 0 && sy == 0, "anti-deadzone: a centred stick stays centred");
    sx = -16384, sy = 0;
    MapIntoTitleDeadzone(sx, sy, kInner, kOuter);
    Check(Near(TitleSteering(sx, sy), -0.5, 0.0001), "anti-deadzone keeps the direction");
    sx = 11585, sy = 11585;  // half deflection on the diagonal
    MapIntoTitleDeadzone(sx, sy, kInner, kOuter);
    Check(sx == sy && Near(TitleMagnitude(sx, sy), 0.5, 0.0001),
          "anti-deadzone: a diagonal keeps its direction and magnitude");
    sx = 9000, sy = 0;
    MapIntoTitleDeadzone(sx, sy, 0.0, 32767.0);
    Check(sx == 9000, "anti-deadzone: an identity range leaves the stick as it is");
    // Without it the first 24% does nothing.
    Check(TitleSteering(int16_t(0.2 * 32767), 0) == 0.0, "without it 20% does not steer");
  }
  // With the runtime's inner deadzone for a stick that does not rest at 0: the rest reads
  // centred, and the first movement past it steers.
  {
    StickShaping stick;
    stick.deadzone = 0.05;
    int16_t sx = 1500, sy = 0;
    Shape(sx, sy, stick);
    MapIntoTitleDeadzone(sx, sy, kInner, kOuter);
    Check(sx == 0 && TitleSteering(sx, sy) == 0.0, "anti-deadzone: a resting stick stays centred");
    sx = 1700, sy = 0;
    Shape(sx, sy, stick);
    MapIntoTitleDeadzone(sx, sy, kInner, kOuter);
    Check(TitleSteering(sx, sy) > 0.0, "anti-deadzone: just past the runtime's deadzone steers");
  }
  // The device merge takes the whole stick that deflects further. The keyboard maps its
  // steering into the same range (StickFromTitle), so with the anti-deadzone the merge
  // compares the two in the title's terms: the larger steering wins and reaches the title
  // unchanged. Without it, a pad's small deflection is inside the title's deadzone and loses
  // to any keyboard steering.
  {
    const rex::input::keyboard::TitleRange range{kInner, kOuter};
    bool larger_wins = true;
    for (const double pad : {0.05, 0.3, 0.5, 0.9}) {
      for (const double keys : {0.1, 0.4, 0.6, 1.0}) {
        int16_t px = int16_t(pad * 32767), py = 0;
        MapIntoTitleDeadzone(px, py, kInner, kOuter);
        const int16_t kx = rex::input::keyboard::StickFromTitle(-keys, range);
        int16_t mx = px, my = py;
        MergeStick(mx, my, kx, 0);
        larger_wins = larger_wins && Near(TitleMagnitude(mx, my), std::fmax(pad, keys), 0.0002);
      }
    }
    Check(larger_wins, "merge: with the anti-deadzone the larger steering of pad and keys wins");
    int16_t px = int16_t(0.2 * 32767), py = 0;  // no anti-deadzone
    int16_t mx = px, my = py;
    MergeStick(mx, my, rex::input::keyboard::StickFromTitle(0.05, range), 0);
    Check(mx != px, "merge: without it a pad's 20% loses to the keys' 5%");
  }

  // Triggers.
  {
    TriggerShaping trigger;
    trigger.deadzone = 0.1;
    trigger.outer = 0.9;
    Check(ShapeTrigger(25, trigger) == 0, "trigger inside its deadzone");
    Check(ShapeTrigger(128, trigger) == 128, "trigger rescaled in the middle");
    Check(ShapeTrigger(230, trigger) == 255, "trigger past its outer edge is full");
    Check(ShapeTrigger(255, trigger) == 255, "full trigger stays full");
    trigger = {};
    trigger.outer = 0.5;
    Check(ShapeTrigger(128, trigger) == 255, "half travel reaches full with outer 0.5");
  }

  // Rumble strength.
  Check(ScaleMotor(1000, 1.5) == 1500, "strength 1.5");
  Check(ScaleMotor(40000, 2.0) == 65535, "strength clamps at full");
  Check(ScaleMotor(40000, 0.0) == 0, "strength 0 is off");

  // Derived trigger rumble: the brake from the right motor, the throttle from the left.
  {
    auto [left, right] = DeriveTriggerRumble(11468, 4915, 0, 255, 1.0);
    Check(left == 0 && right == 11468, "full throttle: rear slip on the throttle trigger");
    std::tie(left, right) = DeriveTriggerRumble(11468, 9209, 255, 0, 1.0);
    Check(left == 9209 && right == 0, "full brake: front slip on the brake trigger");
    std::tie(left, right) = DeriveTriggerRumble(11468, 9209, 30, 30, 1.0);
    Check(left == 0 && right == 0, "triggers at XInput's threshold stay still");
    std::tie(left, right) = DeriveTriggerRumble(0, 0, 255, 255, 2.0);
    Check(left == 0 && right == 0, "zero motors give zero trigger rumble");
    std::tie(left, right) = DeriveTriggerRumble(11468, 0, 0, 142, 1.0);
    Check(Near(right, 11468.0 * 112.0 / 225.0, 1.0), "half throttle scales it");
    std::tie(left, right) = DeriveTriggerRumble(40000, 40000, 255, 255, 2.0);
    Check(left == 65535 && right == 65535, "trigger rumble clamps at full");
  }

  if (failures) {
    std::printf("%d failure(s)\n", failures);
    return 1;
  }
  std::printf("pad shaping tests passed\n");
  return 0;
}
