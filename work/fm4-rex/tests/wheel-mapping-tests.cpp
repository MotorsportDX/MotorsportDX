// CPU tests of the racing wheel's driving controls (rex/input/wheel/wheel_mapping.h,
// docs/analysis/racing-wheels.md, stages 2 and 5): the steering at the device's full resolution
// and its lock, fixed or the car's own, the pedals' calibration (rest and pressed in either order, deadzone, gamma),
// and the buttons and the shift press counters. Returns 1 on failure.

#include <cmath>
#include <cstdio>
#include <vector>

#include <rex/input/wheel/wheel_mapping.h>

namespace {

namespace wheel = rex::input::wheel;

int failures = 0;

void Check(bool condition, const char* what) {
  if (!condition) {
    std::printf("FAIL %s\n", what);
    ++failures;
  }
}

bool Near(double a, double b, double tolerance = 1e-9) { return std::fabs(a - b) <= tolerance; }

}  // namespace

int main() {
  // The steering: the whole axis is -1..1, each raw step a step of the output.
  {
    wheel::SteeringMap map;
    Check(Near(wheel::MapSteering(map, 32767), 1.0), "full right is 1");
    Check(Near(wheel::MapSteering(map, -32768), -1.0), "full left is -1");
    Check(Near(wheel::MapSteering(map, 0), 0.0), "centre is 0");
    const double step = wheel::MapSteering(map, 1) - wheel::MapSteering(map, 0);
    Check(step > 0.0 && step < 1.0 / 128.0, "one raw step is finer than an 8-bit step");
    Check(wheel::MapSteering(map, 101) > wheel::MapSteering(map, 100), "monotonic");
    map.invert = true;
    Check(Near(wheel::MapSteering(map, 32767), -1.0), "inverted");
  }

  // The soft lock: 900 degrees of wheel, full steering at 540.
  {
    wheel::SteeringMap map;
    map.range_deg = 900.0;
    map.lock_deg = 540.0;
    // 135 degrees right of 450 is 0.3 of the axis, 0.5 of the lock.
    Check(Near(wheel::MapSteering(map, int16_t(std::lround(0.3 * 32767.0))), 0.5, 1e-4),
          "half lock at 135 of 270 degrees");
    Check(Near(wheel::MapSteering(map, int16_t(std::lround(0.7 * 32767.0))), 1.0),
          "clamped beyond the lock");
    map.lock_deg = 2000.0;
    Check(Near(wheel::MapSteering(map, 32767), 1.0), "a lock beyond the range is the range");
    map.lock_deg = 0.0;
    map.range_deg = std::nan("");
    Check(Near(wheel::MapSteering(map, 32767), 1.0), "a bad range falls back");
  }

  // The car's own lock (stage 5): 900 degrees for a 42-degree road-wheel lock, scaled per car.
  {
    wheel::SteeringMap map;
    map.range_deg = 900.0;
    Check(Near(wheel::SteeringLockDeg(map, 0.0), 900.0), "no car yet: the range");
    Check(Near(wheel::SteeringLockDeg(map, 42.0), 900.0), "a road car: 900");
    Check(Near(wheel::SteeringLockDeg(map, 36.0), 900.0 * 36.0 / 42.0), "a GT car: 771");
    Check(Near(wheel::SteeringLockDeg(map, 30.0), 900.0 * 30.0 / 42.0), "a prototype: 643");
    // A 540-degree car on a 900-degree wheel reaches full steering at 270 degrees.
    map.car_lock_deg = 540.0;
    const int16_t at270 = int16_t(std::lround(270.0 / 450.0 * 32767.0));
    Check(Near(wheel::MapSteering(map, at270, 42.0), 1.0, 1e-4), "full lock at 270 degrees");
    Check(Near(wheel::MapSteering(map, int16_t(at270 / 2), 42.0), 0.5, 1e-4),
          "half lock at 135 degrees: linear, 1:1");
    Check(Near(wheel::SteeringAngleDeg(map, 32767), 450.0), "the angle is the wheel's own");
    map.invert = true;
    Check(Near(wheel::SteeringAngleDeg(map, 32767), -450.0), "the angle follows the inversion");
    map.invert = false;
    // A fixed lock wins over the car's; a lock past the range is the range.
    map.lock_deg = 720.0;
    Check(Near(wheel::SteeringLockDeg(map, 30.0), 720.0), "a fixed lock wins");
    map.lock_deg = 0.0;
    map.car_lock_deg = 1800.0;
    Check(Near(wheel::SteeringLockDeg(map, 42.0), 900.0), "never more than the range");
    map.car_lock_deg = std::nan("");
    Check(Near(wheel::SteeringLockDeg(map, 42.0), 900.0), "a bad car lock falls back");
    map.car_lock_deg = 900.0;
    Check(Near(wheel::SteeringLockDeg(map, -5.0), 900.0), "a bad road lock falls back");
    // MapControls carries the angle and the lock.
    wheel::MappingConfig config;
    config.steering.range_deg = 900.0;
    const std::vector<int16_t> axes = {int16_t(std::lround(350.0 / 450.0 * 32767.0)), 0, 0};
    const wheel::Controls state = wheel::MapControls(config, axes, {}, {}, 30.0);
    Check(Near(state.lock_deg, 900.0 * 30.0 / 42.0), "the controls carry the lock");
    Check(Near(state.wheel_deg, 350.0, 0.01), "the controls carry the angle");
    Check(Near(state.steering, 1.0), "past the lock the steering is full");
  }

  // The pedals.
  {
    wheel::PedalMap map;  // rests at -32768, pressed at 32767
    map.axis = 1;
    Check(Near(wheel::MapPedal(map, -32768), 0.0), "pedal at rest is 0");
    Check(Near(wheel::MapPedal(map, 32767), 1.0), "pedal pressed is 1");
    Check(Near(wheel::MapPedal(map, 0), 32768.0 / 65535.0), "pedal half way");
    const double step = wheel::MapPedal(map, -32767) - wheel::MapPedal(map, -32768);
    Check(step > 0.0 && step < 1.0 / 255.0, "one raw step is finer than the triggers' 8 bits");

    wheel::PedalMap reversed = map;  // a pedal whose raw value falls as it is pressed
    reversed.released = 32767.0;
    reversed.pressed = -32768.0;
    Check(Near(wheel::MapPedal(reversed, 32767), 0.0), "reversed pedal at rest");
    Check(Near(wheel::MapPedal(reversed, -32768), 1.0), "reversed pedal pressed");

    wheel::PedalMap partial = map;  // calibrated to the travel actually used
    partial.released = -30000.0;
    partial.pressed = 20000.0;
    Check(Near(wheel::MapPedal(partial, -32768), 0.0), "below rest clamps to 0");
    Check(Near(wheel::MapPedal(partial, 30000), 1.0), "beyond pressed clamps to 1");

    wheel::PedalMap dead = map;
    dead.deadzone = 0.1;
    Check(Near(wheel::MapPedal(dead, int16_t(-32768 + 6000)), 0.0), "inside the deadzone");
    Check(wheel::MapPedal(dead, int16_t(-32768 + 7000)) > 0.0, "past the deadzone");
    Check(Near(wheel::MapPedal(dead, 32767), 1.0), "the deadzone keeps the full travel");

    wheel::PedalMap curved = map;
    curved.gamma = 2.0;
    Check(Near(wheel::MapPedal(curved, 0), std::pow(32768.0 / 65535.0, 2.0)), "gamma 2");
    curved.gamma = std::nan("");
    Check(Near(wheel::MapPedal(curved, 32767), 1.0), "a bad gamma falls back to linear");

    wheel::PedalMap flat = map;
    flat.pressed = flat.released;
    Check(Near(wheel::MapPedal(flat, 32767), 0.0), "no travel reads 0");
  }

  // The whole state: axes, buttons, missing axes, and the press counters.
  {
    wheel::MappingConfig config;
    config.shift_up_button = 4;
    config.shift_down_button = 5;
    config.clutch_button = 0;
    config.handbrake_button = 1;
    std::vector<int16_t> axes = {16384, 32767, -32768};
    std::vector<uint8_t> buttons(8, 0);
    wheel::Controls state;
    state = wheel::MapControls(config, axes, buttons, state);
    Check(state.connected, "connected");
    Check(Near(state.steering, 16384.0 / 32767.0), "steering from axis 0");
    Check(Near(state.throttle, 1.0) && Near(state.brake, 0.0), "pedals from axes 1 and 2");
    Check(state.clutch == 0.0 && state.handbrake == 0.0, "no buttons held");
    buttons[4] = 1;
    buttons[0] = 1;
    buttons[1] = 1;
    state = wheel::MapControls(config, axes, buttons, state);
    Check(state.shift_up && state.shift_up_presses == 1, "a shift up press counts");
    Check(state.clutch == 1.0 && state.handbrake == 1.0, "clutch and handbrake buttons");
    state = wheel::MapControls(config, axes, buttons, state);
    Check(state.shift_up_presses == 1, "a held shift counts once");
    buttons[4] = 0;
    state = wheel::MapControls(config, axes, buttons, state);
    buttons[4] = 1;
    state = wheel::MapControls(config, axes, buttons, state);
    Check(state.shift_up_presses == 2 && state.shift_down_presses == 0, "a second press counts");

    config.brake.axis = 7;  // beyond the device's axes
    config.steering.axis = 9;
    state = wheel::MapControls(config, axes, buttons, state);
    Check(state.brake == 0.0 && state.steering == 0.0, "missing axes read 0");
    config.shift_up_button = 40;
    state = wheel::MapControls(config, axes, buttons, state);
    Check(!state.shift_up, "a missing button is not held");
  }

  if (failures) {
    std::printf("%d failures\n", failures);
    return 1;
  }
  std::printf("wheel mapping tests passed\n");
  return 0;
}
