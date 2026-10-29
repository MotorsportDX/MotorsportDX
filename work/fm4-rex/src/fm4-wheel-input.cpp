// The racing wheel's driving controls in the title, and the frame rate of its anti-cheat rule
// (docs/analysis/racing-wheels.md, stage 2).
//
// Once per frame the race update's control step (sub_826D5E78) hands every car of the race its
// 28-byte control record; a player's it builds from the player's pad object: throttle, brake, steering, clutch and handbrake
// from the layout's actions (sub_8238A468 unipolar, sub_822B35C0 bipolar, both on the floats
// the input conversion wrote), shift up and down from button actions (sub_822D0F48), the wheel
// mode and the gear. It hands the record to the car's CCarDynamics (slot 14, sub_823651D8:
// a copy to +0xE90) and then counts the frame for the anti-cheat rule (sub_823B1560).
//
// The wheel host publishes its mapped controls (rex/input/wheel/wheel_controls.h). Here the
// action reads of that step return them for the first player's pad, merged as the title merges
// a layout's bindings: the larger value of a pedal, the steering of larger magnitude, a shift
// on a new press. So the steering reaches the record at the wheel's resolution, after the
// pad's deadzones and before everything the title does with the record; the title's layouts,
// menus, pause and AI driving are untouched, and the device class stays the pad's.
//
// The record's wheel mode byte picks the car's steering filters. A traced run (stage 2,
// 29 October 2026) measured them on one steering script: mode 0, the pad's, limits the rate
// (a full-lock step takes 360 ms to 90%); mode 1, the 270-degree wheel's, bends the response
// (0.25 of the input gives 0.18 of the lock); mode 2, the 900-degree wheel's, follows the input
// linearly and settles a full-lock step in 50 ms. So a record the wheel's steering drives gets
// mode 2, and the pad keeps mode 0 whenever its steering wins the merge.
//
// The anti-cheat rule (stage 1 findings) counts a frame whose steering differs from the car's
// previous one by more than FrameAnalogInputThreshold (0.0075), and withholds the race's
// credits and experience when a completed lap has fewer than MinInputsPerLap (10) of them. The
// threshold is a change per frame, so the faster the frame rate, the faster a turn must be to
// count. The count's hook scales the change by the frame's time step, so the threshold means
// what it does at 60 fps (the main session's decision of 29 October 2026): a change counts
// when it exceeds 0.0075 x (dt x 60), dt being the session time the title advanced in this
// frame. It applies to every device; FM4_INPUT_THRESHOLD_UNSCALED=1 keeps the title's rule.
//
// FM4_WHEEL_TRACE_FILE=<path> writes one line per event, nothing without it:
//   rec <ms> u=<updates> dyn=<dynamics> user=<u> wheel=<0|1> <throttle> <brake>
//       <steering> <clutch> <handbrake> up=<0|1> down=<0|1> b8=<0|1> mode=<m> gear=<g>
//       the record as CCarDynamics gets it, every car; user is the pad's whose steering was
//       read (-1: none, an AI's or a replay's record), wheel=1 when the wheel's steering won
//   cnt <ms> car=<car> prev=<p> new=<n> dt=<s> scale=<k> raw=<0|1> scaled=<0|1> count=<c>
//       flag=<+0x38F8> in=<0|1> the anti-cheat count after the frame; raw and scaled say
//       whether the change exceeds the threshold unscaled and scaled, `in` whether the session
//       update was in progress (else dt is the last update's)
//   lap <ms> car=<car> count=<c> few=<0|1> the lap check (sub_823B1648); few=1 marks the race
//   draw <ms> obj=<presentation> angle=<a> b=<f2> the drawn steering wheel (sub_822E6B30),
//       every car, with FM4_WHEEL_TRACE_DRAWN=1
// FM4_WHEEL_DUMP_FILE=<path>: per record of the wheel's car, a u32 update count, a float of
// the session time and the first 0x2000 bytes of its CCarDynamics, big-endian as in the guest.
// FM4_WHEEL_MODE=<0|1|2> sets the record's wheel mode byte (+0x17) when the wheel's steering
// drives it (otherwise 2), and cycle:<s> steps it through 0, 1 and 2 every s seconds of session
// time: for the traced runs that compare the title's steering filters per mode.
//
// Force feedback (stage 3). For each player's car the control step reads the steering torque
// (CCarDynamics slot 254), the speed and a load-weighted rear slip, zeroes the torque unless the
// player drives the car (the car's +0x38F8 clear and sub_822B7130 false: not the AI, not a
// replay), and hands the three to the pad's force feedback block (pad + 0x1090) through
// sub_8239E600, a tail call of sub_828C3330. sub_828C3330 stores the dynamic force,
// clamp(DynFFBScale x torque, +-min(DynFFBMaxForce, 1)), at +0x538, and the spring's scale, a
// speed curve times a rear slip curve, at +0x540. The control step then stores whether the
// player drives (sub_823921F8, the block's +5) for the title's own device update, which sends
// no force without it. The block also holds ControllerFFB.ini (sub_828C2CD8: the damper at
// +0x20, the spring at +0x2C, scale then maximum force) and ForceFeedbackScale at +0x53C, which
// scales every effect (sub_828C34E0). Here the hook of sub_828C3330 takes the block's results and
// the hook of the flag's store publishes them for the first player's pad, scaled as the title
// scales its effects: the constant force +0x538 x ForceFeedbackScale, the spring's coefficient
// and saturation SpringScale and SpringMaxForce x ForceFeedbackScale x the spring's scale, the
// damper's DamperScale and DamperMaxForce x ForceFeedbackScale. A race update whose control step
// finds no player's pad, or that skips the control step, resets the force feedback blocks
// (sub_828C5B18): that publishes a sample outside a driven race, which zeroes the force at once.
// A race update that stops (pause, menus, loading) publishes nothing, and the host's watchdog
// takes the force to zero.
//
// FM4_WHEEL_TRACE_FILE also gets, per sample:
//   ffb <ms> u=<updates> drv=<0|1> torque=<t> speed=<v> slip=<s> force=<+0x538> ffs=<+0x53C>
//       sscale=<+0x540> const=<c> spring=<coefficient>/<saturation> damper=<c>/<s>
//   ffb <ms> u=<updates> reset   a reset of the blocks (once per run of resets)
//
// The force at the physics rate (stage 4). The torque at CCarDynamics +0x30 is written once
// per car step, not per physics step: a write watch (fm4-write-watch.cpp, 29 October 2026)
// found one writer, sub_823021F0 (the steering torque model), called from sub_82302A18 at the
// end of the car's step sub_82822BD0, once per frame at 60 and at 240 fps. The car step first
// integrates its physics in m steps of dt/m (sub_82304E80, sub_82307448, sub_823071B0,
// sub_82304ED8; m from the frame's dt and the physics rate), and the torque model then reads
// the front tyres' state the last step left. Here, after each physics step of the first
// player's car, the title's own torque model is evaluated on that step's state, with the time
// since the car step began, and the bytes it writes are put back (its smoothing and steering
// rate state at the car's +2440 to +2484, and the front tyres' +800 to +832): so the title's
// state is unchanged, and the last step's value is the title's own torque. Each value goes to
// the host as a point at its physics time (rex/input/wheel/wheel_controls.h), scaled as the
// force feedback block scales the frame's torque. FM4_WHEEL_PHYSICS=0 publishes no points.
//
// FM4_WHEEL_TRACE_FILE also gets, per car step of that car:
//   phys <ms> u=<updates> steps=<m> dt=<s> last=<the last step's value> title=<the title's
//       torque after the step>; the two are equal when the evaluation reproduces the title
#include <rex/hook.h>
#include <rex/input/wheel/wheel_controls.h>
#include <rex/memory.h>

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

#include "fm4-diagnostic-state.h"

REX_EXTERN(__imp__sub_8238A468);
REX_EXTERN(__imp__sub_822B35C0);
REX_EXTERN(__imp__sub_822D0F48);
REX_EXTERN(__imp__sub_823651D8);
REX_EXTERN(__imp__sub_823B1560);
REX_EXTERN(__imp__sub_823B1648);
REX_EXTERN(__imp__sub_822E6B30);
REX_EXTERN(__imp__sub_828C3330);
REX_EXTERN(__imp__sub_823921F8);
REX_EXTERN(__imp__sub_828C5B18);
REX_EXTERN(__imp__sub_828154B0);
REX_EXTERN(__imp__sub_8239C970);
REX_EXTERN(__imp__sub_82822BD0);
REX_EXTERN(__imp__sub_82304E80);
REX_EXTERN(__imp__sub_82304ED8);
REX_EXTERN(__imp__sub_823021F0);

// fm4-write-watch.cpp.
void FM4WriteWatch(uint8_t* base, uint32_t address);

namespace {

// sub_826D5E78's return addresses from its reads of the control record's actions.
constexpr uint32_t kThrottleRead = 0x826D61AC;   // sub_8238A468, action 0
constexpr uint32_t kBrakeRead = 0x826D61C8;      // sub_8238A468, action 1
constexpr uint32_t kSteeringRead = 0x826D61DC;   // sub_822B35C0, action 2
constexpr uint32_t kClutchRead = 0x826D6248;     // sub_8238A468, action 6
constexpr uint32_t kHandbrakeRead = 0x826D625C;  // sub_8238A468, action 3
constexpr uint32_t kShiftUpRead = 0x826D62C0;    // sub_822D0F48, action 4
constexpr uint32_t kShiftDownRead = 0x826D62D8;  // sub_822D0F48, action 5
// ... from handing the record to CCarDynamics slot 14 (sub_823651D8).
constexpr uint32_t kRecordSet = 0x826D69EC;
// The guest user whose pad the wheel drives (the pad object's first word, low byte).
constexpr uint32_t kWheelUser = 0;
// The record's wheel mode for the wheel's steering: the title's 900-degree wheel filters.
constexpr uint8_t kWheelMode = 2;
// GameSettings' CheatDetection object (stage 1 findings): FrameAnalogInputThreshold at +0x18.
constexpr uint32_t kCheatSettings = 0x82DC372C;
constexpr double kTitleFrameRate = 60.0;
// The pad's force feedback block and its fields (stage 3).
constexpr uint32_t kForceBlock = 0x1090;
constexpr uint32_t kDamperScale = 0x20;
constexpr uint32_t kDamperMaxForce = 0x24;
constexpr uint32_t kSpringScale = 0x2C;
constexpr uint32_t kSpringMaxForce = 0x30;
constexpr uint32_t kDynamicForce = 0x538;
constexpr uint32_t kForceFeedbackScale = 0x53C;
constexpr uint32_t kSpringSpeedSlipScale = 0x540;
// sub_826D5E78's return address from storing whether the player drives (sub_823921F8), right
// after the force feedback block's update.
constexpr uint32_t kDrivingSet = 0x826D6788;
// ... from reading the steering torque (CCarDynamics slot 254, sub_828154B0).
constexpr uint32_t kTorqueRead = 0x826D6754;
constexpr uint32_t kTorqueOffset = 0x30;
// ... from the rumble update (sub_8239C970: the pad, the frame's step, the rumble inputs).
constexpr uint32_t kRumbleUpdate = 0x826D66F4;
// The pad's rumble object and its <Wheel> table (ControllerRumble.xml; +1792 is the
// <Controller> table, which the pad class uses). A table holds a left and a right curve per
// effect, 64 bytes each: three points (x, y) at +0, +16 and +32.
constexpr uint32_t kRumbleObject = 688;
constexpr uint32_t kWheelTable = 32;
constexpr uint32_t kCurveRight = 64;
constexpr uint32_t kCarCollisionCurve = 256;
constexpr uint32_t kCarCollisionTimeCurve = 384;
constexpr uint32_t kWorldCollisionCurve = 528;
constexpr uint32_t kWorldCollisionTimeCurve = 656;
constexpr uint32_t kObjectCollisionCurve = 800;
constexpr uint32_t kObjectCollisionTimeCurve = 928;
constexpr uint32_t kSurfaceBumpinessCurve = 1584;
// The rumble inputs (r5): the collision impulses with cars, the world and objects, and the
// surface bumpiness, among the engine speed and the tyres' traction.
constexpr uint32_t kCarCollisionInput = 8;
constexpr uint32_t kWorldCollisionInput = 12;
constexpr uint32_t kObjectCollisionInput = 16;
constexpr uint32_t kSurfaceBumpinessInput = 36;
// The car's physics object is CCarDynamics - 0x960; the torque model's state in it, and the
// front tyres (index 0 and 1, 1056 bytes each from +6160) with the state the model writes.
constexpr uint32_t kPhysicsFromDynamics = 0x960;
constexpr uint32_t kTorqueStateBegin = 2440;
constexpr uint32_t kTorqueStateEnd = 2484;
constexpr uint32_t kTyres = 6160;
constexpr uint32_t kTyreSize = 1056;
constexpr uint32_t kTyreStateBegin = 800;
constexpr uint32_t kTyreStateEnd = 832;
// The block's dynamic force settings (ControllerFFB.ini): DynFFBScale and DynFFBMaxForce.
constexpr uint32_t kDynamicScale = 0x38;
constexpr uint32_t kDynamicMaxForce = 0x3C;

uint32_t Load32(uint8_t* base, uint32_t address) {
  return rex::memory::load_and_swap<uint32_t>(base + address);
}
float LoadFloat(uint8_t* base, uint32_t address) {
  return std::bit_cast<float>(Load32(base, address));
}

double SteadyMs() {
  using namespace std::chrono;
  return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

FILE* OpenFromEnv(const char* variable, const char* mode) {
  const char* path = std::getenv(variable);
  if (!path || !*path) return nullptr;
  FILE* file = std::fopen(path, mode);
  if (!file) std::fprintf(stderr, "FM4 wheel: cannot open %s\n", path);
  return file;
}

FILE* Trace() {
  static FILE* file = OpenFromEnv("FM4_WHEEL_TRACE_FILE", "w");
  return file;
}
FILE* Dump() {
  static FILE* file = OpenFromEnv("FM4_WHEEL_DUMP_FILE", "wb");
  return file;
}

std::mutex trace_mutex;
unsigned trace_lines = 0;
constexpr unsigned kTraceLimit = 600000;

void TraceLine(const char* format, ...) {
  FILE* file = Trace();
  if (!file) return;
  std::lock_guard lock(trace_mutex);
  if (trace_lines >= kTraceLimit) return;
  ++trace_lines;
  va_list args;
  va_start(args, format);
  std::vfprintf(file, format, args);
  va_end(args);
  std::fputc('\n', file);
  // The e2e harness kills the game, so the trace is flushed as it goes.
  if (trace_lines % 64 == 0) std::fflush(file);
}

bool Unscaled() {
  static const bool unscaled = []() {
    const char* value = std::getenv("FM4_INPUT_THRESHOLD_UNSCALED");
    return value && std::strcmp(value, "1") == 0;
  }();
  return unscaled;
}

// FM4_WHEEL_MODE: -1 none, 0..2 a fixed mode, 3 the cycle.
struct ModeOverride {
  int mode = -1;
  double cycle_s = 0.0;
};
const ModeOverride& Mode() {
  static const ModeOverride mode = []() {
    ModeOverride out;
    const char* value = std::getenv("FM4_WHEEL_MODE");
    if (!value || !*value) return out;
    if (std::strncmp(value, "cycle:", 6) == 0) {
      out.cycle_s = std::atof(value + 6);
      out.mode = out.cycle_s > 0.0 ? 3 : -1;
    } else if (value[0] >= '0' && value[0] <= '2' && !value[1]) {
      out.mode = value[0] - '0';
    }
    return out;
  }();
  return mode;
}

// The control step runs on the title's main thread, one car at a time: the steering read marks
// a record whose steering the wheel won, and its hand-over to CCarDynamics follows in the same
// pass.
thread_local bool wheel_drove_record = false;
// The user of the pad whose steering was read for the record (-1: not read, as when the AI
// drives), for the trace.
thread_local int record_user = -1;
thread_local uint32_t shift_up_seen = 0;
thread_local uint32_t shift_down_seen = 0;
thread_local bool shift_seen_valid = false;

// The wheel's controls for this read, when it drives this pad.
bool WheelControls(uint8_t* base, uint32_t pad, rex::input::wheel::Controls* controls) {
  if (!pad || (Load32(base, pad) & 0xFF) != kWheelUser) return false;
  return rex::input::wheel::GetControls(controls);
}

}  // namespace

// The unipolar action reads: throttle, brake, clutch and handbrake.
REX_HOOK_RAW(sub_8238A468) {
  const uint32_t from = uint32_t(ctx.lr);
  const uint32_t pad = ctx.r3.u32;
  __imp__sub_8238A468(ctx, base);
  if (from != kThrottleRead && from != kBrakeRead && from != kClutchRead &&
      from != kHandbrakeRead) {
    return;
  }
  rex::input::wheel::Controls controls;
  if (!WheelControls(base, pad, &controls)) return;
  double value = 0.0;
  switch (from) {
    case kThrottleRead: value = controls.throttle; break;
    case kBrakeRead: value = controls.brake; break;
    case kClutchRead: value = controls.clutch; break;
    case kHandbrakeRead: value = controls.handbrake; break;
  }
  ctx.f1.f64 = std::max(ctx.f1.f64, double(float(value)));
}

// The bipolar action read: the steering.
REX_HOOK_RAW(sub_822B35C0) {
  const uint32_t from = uint32_t(ctx.lr);
  const uint32_t pad = ctx.r3.u32;
  __imp__sub_822B35C0(ctx, base);
  if (from != kSteeringRead) return;
  record_user = pad ? int(Load32(base, pad) & 0xFF) : -1;
  rex::input::wheel::Controls controls;
  if (!WheelControls(base, pad, &controls)) return;
  const double steering = double(float(controls.steering));
  wheel_drove_record = std::fabs(steering) >= std::fabs(ctx.f1.f64);
  if (wheel_drove_record) ctx.f1.f64 = steering;
}

// The button action reads: a shift on each new press of the wheel's shift buttons.
REX_HOOK_RAW(sub_822D0F48) {
  const uint32_t from = uint32_t(ctx.lr);
  const uint32_t pad = ctx.r3.u32;
  __imp__sub_822D0F48(ctx, base);
  if (from != kShiftUpRead && from != kShiftDownRead) return;
  rex::input::wheel::Controls controls;
  if (!WheelControls(base, pad, &controls)) {
    shift_seen_valid = false;
    return;
  }
  // The first read after the wheel appears takes the counts as they are: no shift for
  // presses made before.
  if (!shift_seen_valid) {
    shift_up_seen = controls.shift_up_presses;
    shift_down_seen = controls.shift_down_presses;
    shift_seen_valid = true;
  }
  bool pressed = false;
  if (from == kShiftUpRead) {
    pressed = controls.shift_up_presses != shift_up_seen;
    shift_up_seen = controls.shift_up_presses;
  } else {
    pressed = controls.shift_down_presses != shift_down_seen;
    shift_down_seen = controls.shift_down_presses;
  }
  if (pressed) ctx.r3.u64 = 1;
}

// CCarDynamics slot 14 (dynamics, record): the record's copy to +0xE90.
REX_HOOK_RAW(sub_823651D8) {
  if (uint32_t(ctx.lr) != kRecordSet) {
    __imp__sub_823651D8(ctx, base);
    return;
  }
  const uint32_t dynamics = ctx.r3.u32, record = ctx.r4.u32;
  const bool wheel = wheel_drove_record;
  const int user = record_user;
  wheel_drove_record = false;
  record_user = -1;
  if (wheel) {
    const ModeOverride& mode = Mode();
    uint8_t value = kWheelMode;
    if (mode.mode == 3) {
      const double t = fm4_diagnostic_session_time.load(std::memory_order_relaxed);
      value = uint8_t(int64_t(std::floor(t / mode.cycle_s)) % 3);
    } else if (mode.mode >= 0) {
      value = uint8_t(mode.mode);
    }
    base[record + 0x17] = value;
  }
  if (Trace()) {
    TraceLine("rec %.2f u=%u dyn=%08x user=%d wheel=%d %.6f %.6f %.6f %.6f %.6f up=%u down=%u "
              "b8=%u mode=%u gear=%u",
              SteadyMs(), fm4_diagnostic_updates.load(std::memory_order_relaxed), dynamics, user,
              wheel ? 1 : 0, LoadFloat(base, record), LoadFloat(base, record + 4),
              LoadFloat(base, record + 8), LoadFloat(base, record + 12),
              LoadFloat(base, record + 16), base[record + 0x14], base[record + 0x15],
              base[record + 0x16], base[record + 0x17], base[record + 0x18]);
  }
  if (wheel && Dump()) {
    static std::mutex dump_mutex;
    std::lock_guard lock(dump_mutex);
    const uint32_t update = fm4_diagnostic_updates.load(std::memory_order_relaxed);
    const float time = float(fm4_diagnostic_session_time.load(std::memory_order_relaxed));
    std::fwrite(&update, sizeof(update), 1, Dump());
    std::fwrite(&time, sizeof(time), 1, Dump());
    // Before the copy: the state the last frame's physics left.
    std::fwrite(base + dynamics, 1, 0x2000, Dump());
  }
  __imp__sub_823651D8(ctx, base);
}

// The anti-cheat count (f1 = the car's previous steering, f2 = this frame's, r3 = the car).
REX_HOOK_RAW(sub_823B1560) {
  const uint32_t car = ctx.r3.u32;
  const double previous = ctx.f1.f64, steering = ctx.f2.f64;
  bool in_update = false;
  double dt = 0.0;
  if (const uint32_t session = fm4_session_updating.load(std::memory_order_relaxed)) {
    in_update = true;
    dt = std::bit_cast<double>(rex::memory::load_and_swap<uint64_t>(base + session + 0x18)) -
         fm4_session_time_before.load(std::memory_order_relaxed);
  } else {
    dt = fm4_session_last_step.load(std::memory_order_relaxed);
  }
  dt = std::fabs(dt);
  double scale = std::isfinite(dt) && dt > 0.0 ? std::clamp(dt * kTitleFrameRate, 0.05, 4.0)
                                               : 1.0;
  if (Unscaled()) scale = 1.0;
  if (scale != 1.0) ctx.f2.f64 = previous + (steering - previous) / scale;
  __imp__sub_823B1560(ctx, base);
  if (!Trace()) return;
  const uint32_t settings = Load32(base, kCheatSettings);
  const double threshold = settings ? double(LoadFloat(base, settings + 0x18)) : 0.0075;
  const double change = std::fabs(float(steering - previous));
  TraceLine("cnt %.2f car=%08x prev=%.6f new=%.6f dt=%.6f scale=%.4f raw=%d scaled=%d count=%u "
            "flag=%u in=%d",
            SteadyMs(), car, previous, steering, dt, scale, change > threshold ? 1 : 0,
            change > threshold * scale ? 1 : 0, Load32(base, car + 0x3938), base[car + 0x38F8],
            in_update ? 1 : 0);
}

// The lap check (car): true when the completed laps had too few counted frames.
REX_HOOK_RAW(sub_823B1648) {
  const uint32_t car = ctx.r3.u32;
  __imp__sub_823B1648(ctx, base);
  TraceLine("lap %.2f car=%08x count=%u few=%u", SteadyMs(), car, Load32(base, car + 0x3938),
            ctx.r3.u32 & 0xFF);
}

// CCarPresentationDeferred::SetSteeringWheelAngle (f1 = angle, f2, r3 = presentation).
REX_HOOK_RAW(sub_822E6B30) {
  static const bool drawn = std::getenv("FM4_WHEEL_TRACE_DRAWN") != nullptr;
  if (drawn && Trace()) {
    TraceLine("draw %.2f obj=%08x angle=%.6f b=%.6f", SteadyMs(), ctx.r3.u32, ctx.f1.f64,
              ctx.f2.f64);
  }
  __imp__sub_822E6B30(ctx, base);
}

namespace {

// The force feedback block's results of this car, until the flag's store publishes them.
thread_local uint32_t pending_block = 0;
thread_local rex::input::wheel::ForceSample pending_sample;
std::atomic<bool> reset_traced{false};

double Unit(double value) { return std::isfinite(value) ? std::clamp(value, 0.0, 1.0) : 0.0; }

// A rumble curve at x, as the title evaluates it (sub_828C42A8): its first point's y below the
// first x, its last point's above the last x, linear between.
double Curve(uint8_t* base, uint32_t curve, double x) {
  const double x0 = LoadFloat(base, curve), y0 = LoadFloat(base, curve + 4);
  const double x1 = LoadFloat(base, curve + 16), y1 = LoadFloat(base, curve + 20);
  const double x2 = LoadFloat(base, curve + 32), y2 = LoadFloat(base, curve + 36);
  if (!std::isfinite(x)) return 0.0;
  if (x <= x0) return y0;
  if (x >= x2) return y2;
  if (x <= x1) return x1 > x0 ? y0 + (y1 - y0) * (x - x0) / (x1 - x0) : y1;
  return x2 > x1 ? y1 + (y2 - y1) * (x - x1) / (x2 - x1) : y2;
}

// The stronger motor's level of an effect in the <Wheel> table.
double WheelLevel(uint8_t* base, uint32_t table, uint32_t curve, double x) {
  return Unit(std::max(Curve(base, table + curve, x), Curve(base, table + curve + kCurveRight, x)));
}

// The road and impact effects of the wheel's pad, from its rumble inputs (stage 4). An impact
// starts when a collision's level exceeds what is left of the last one (the title fades a
// collision's rumble over its time curve), so a scrape along a wall does not restart it every
// frame.
struct Effects {
  double road = 0.0;
  double impact = 0.0;
  double impact_s = 0.0;
  uint32_t impacts = 0;
  double impact_start = 0.0;
  // The inputs, for the trace, and the <Wheel> table's surface level at its last point.
  double bumpiness = 0.0;
  double car = 0.0, world = 0.0, object = 0.0;
  double table_y2 = 0.0;
};
thread_local Effects effects;
thread_local bool effects_fresh = false;
thread_local double pending_dynamic_scale = 1.0;
thread_local double pending_dynamic_max = 1.0;
thread_local double pending_feedback_scale = 1.0;

// The first player's car for the physics-rate force, from the control step: its physics
// object, whether the player drives it, and the force feedback block's scaling of the torque.
std::atomic<uint32_t> player_physics{0};
std::atomic<bool> player_driving{false};
std::atomic<double> dynamic_scale{1.0};
std::atomic<double> dynamic_max{1.0};
std::atomic<double> feedback_scale{1.0};

}  // namespace

// The force feedback block's update (r3 = the block, r4 = torque, speed and rear slip).
REX_HOOK_RAW(sub_828C3330) {
  const uint32_t block = ctx.r3.u32, in = ctx.r4.u32;
  __imp__sub_828C3330(ctx, base);
  rex::input::wheel::ForceSample& sample = pending_sample;
  const double scale = Unit(LoadFloat(base, block + kForceFeedbackScale));
  const double spring_scale = Unit(LoadFloat(base, block + kSpringSpeedSlipScale));
  const double force = LoadFloat(base, block + kDynamicForce);
  sample.constant = std::isfinite(force) ? std::clamp(force, -1.0, 1.0) * scale : 0.0;
  sample.spring = {Unit(LoadFloat(base, block + kSpringScale) * scale) * spring_scale,
                   Unit(LoadFloat(base, block + kSpringMaxForce) * scale) * spring_scale};
  sample.damper = {Unit(LoadFloat(base, block + kDamperScale) * scale),
                   Unit(LoadFloat(base, block + kDamperMaxForce) * scale)};
  sample.torque = LoadFloat(base, in);
  sample.speed = LoadFloat(base, in + 4);
  sample.slip = LoadFloat(base, in + 8);
  pending_block = block;
  const double dyn_scale = LoadFloat(base, block + kDynamicScale);
  const double dyn_max = LoadFloat(base, block + kDynamicMaxForce);
  pending_dynamic_scale = std::isfinite(dyn_scale) ? dyn_scale : 0.0;
  pending_dynamic_max = std::isfinite(dyn_max) ? std::max(dyn_max, 0.0) : 0.0;
  pending_feedback_scale = scale;
}

// The rumble update (r3 = the pad, f1 = the frame's step, r5 = the rumble inputs): the road's
// vibration and the impacts for the wheel's pad.
REX_HOOK_RAW(sub_8239C970) {
  const uint32_t from = uint32_t(ctx.lr);
  const uint32_t pad = ctx.r3.u32, in = ctx.r5.u32;
  __imp__sub_8239C970(ctx, base);
  if (from != kRumbleUpdate || !pad || !in || (Load32(base, pad) & 0xFF) != kWheelUser) return;
  const uint32_t table = pad + kRumbleObject + kWheelTable;
  Effects& e = effects;
  e.bumpiness = LoadFloat(base, in + kSurfaceBumpinessInput);
  e.car = LoadFloat(base, in + kCarCollisionInput);
  e.world = LoadFloat(base, in + kWorldCollisionInput);
  e.object = LoadFloat(base, in + kObjectCollisionInput);
  e.table_y2 = LoadFloat(base, table + kSurfaceBumpinessCurve + 36);
  e.road = e.bumpiness > 0.0 ? WheelLevel(base, table, kSurfaceBumpinessCurve, e.bumpiness) : 0.0;
  double level = 0.0, length = 0.0;
  const auto collision = [&](double x, uint32_t curve, uint32_t time_curve) {
    if (!(x > 0.0)) return;
    const double l = WheelLevel(base, table, curve, x);
    if (l > level) {
      level = l;
      length = std::clamp(Curve(base, table + time_curve, x), 0.02, 1.0);
    }
  };
  collision(e.car, kCarCollisionCurve, kCarCollisionTimeCurve);
  collision(e.world, kWorldCollisionCurve, kWorldCollisionTimeCurve);
  collision(e.object, kObjectCollisionCurve, kObjectCollisionTimeCurve);
  const double now = fm4_diagnostic_session_time.load(std::memory_order_relaxed);
  const double elapsed = now - e.impact_start;
  const double left = e.impact_s > 0.0 && elapsed >= 0.0 && elapsed < e.impact_s
                          ? e.impact * (1.0 - elapsed / e.impact_s)
                          : 0.0;
  if (level > 0.0 && level > left * 1.25) {
    e.impact = level;
    e.impact_s = length;
    e.impact_start = now;
    ++e.impacts;
  }
  effects_fresh = true;
}

// The store of whether the player drives the car (r3 = the pad, r4 = the flag).
REX_HOOK_RAW(sub_823921F8) {
  const uint32_t from = uint32_t(ctx.lr);
  const uint32_t pad = ctx.r3.u32;
  const bool driving = ctx.r4.u8 != 0;
  __imp__sub_823921F8(ctx, base);
  if (from != kDrivingSet) return;
  const uint32_t block = pending_block;
  pending_block = 0;
  if (!pad || block != pad + kForceBlock || (Load32(base, pad) & 0xFF) != kWheelUser) return;
  rex::input::wheel::ForceSample& sample = pending_sample;
  sample.driving = driving;
  if (effects_fresh) {
    effects_fresh = false;
    sample.road = effects.road;
    sample.impact = effects.impact;
    sample.impact_s = effects.impact_s;
    sample.impacts = effects.impacts;
  } else {
    sample.road = 0.0;
  }
  dynamic_scale.store(pending_dynamic_scale, std::memory_order_relaxed);
  dynamic_max.store(pending_dynamic_max, std::memory_order_relaxed);
  feedback_scale.store(pending_feedback_scale, std::memory_order_relaxed);
  player_driving.store(driving, std::memory_order_relaxed);
  rex::input::wheel::PublishForces(sample);
  reset_traced.store(false, std::memory_order_relaxed);
  if (Trace()) {
    TraceLine("ffb %.2f u=%u drv=%d torque=%.6f speed=%.4f slip=%.4f force=%.6f ffs=%.4f "
              "sscale=%.4f const=%.6f spring=%.4f/%.4f damper=%.4f/%.4f bump=%.4f road=%.4f "
              "hit=%.3f/%.3f/%.3f impact=%.4f/%.3f/%u y2=%.3f",
              SteadyMs(), fm4_diagnostic_updates.load(std::memory_order_relaxed), driving ? 1 : 0,
              sample.torque, sample.speed, sample.slip, LoadFloat(base, block + kDynamicForce),
              LoadFloat(base, block + kForceFeedbackScale),
              LoadFloat(base, block + kSpringSpeedSlipScale), sample.constant,
              sample.spring.coefficient, sample.spring.saturation, sample.damper.coefficient,
              sample.damper.saturation, effects.bumpiness, sample.road, effects.car,
              effects.world, effects.object, sample.impact, sample.impact_s, sample.impacts,
              effects.table_y2);
  }
}

// The reset of the force feedback blocks: a race update without a player's control.
REX_HOOK_RAW(sub_828C5B18) {
  __imp__sub_828C5B18(ctx, base);
  player_driving.store(false, std::memory_order_relaxed);
  rex::input::wheel::PublishForces({});
  if (Trace() && !reset_traced.exchange(true, std::memory_order_relaxed)) {
    TraceLine("ffb %.2f u=%u reset", SteadyMs(),
              fm4_diagnostic_updates.load(std::memory_order_relaxed));
  }
}

namespace {

bool PhysicsOn() {
  static const bool on = []() {
    const char* value = std::getenv("FM4_WHEEL_PHYSICS");
    return !value || std::strcmp(value, "0") != 0;
  }();
  return on;
}

// The car step of the player's car on this thread: its physics object, the physics steps run,
// the time since the step began, and the physics clock of the points.
thread_local uint32_t step_physics = 0;
thread_local int step_count = 0;
thread_local double step_elapsed = 0.0;
thread_local double step_dt = 0.0;
thread_local double step_last = 0.0;
std::atomic<double> physics_clock{0.0};

// The title's torque model on the state of the step just run, with its writes put back.
double EvaluateTorque(PPCContext& ctx, uint8_t* base, uint32_t physics, double elapsed) {
  uint8_t state[kTorqueStateEnd - kTorqueStateBegin];
  uint8_t tyres[2][kTyreStateEnd - kTyreStateBegin];
  std::memcpy(state, base + physics + kTorqueStateBegin, sizeof(state));
  for (uint32_t i = 0; i < 2; ++i) {
    std::memcpy(tyres[i], base + physics + kTyres + i * kTyreSize + kTyreStateBegin,
                sizeof(tyres[i]));
  }
  const uint32_t torque_offset = kPhysicsFromDynamics + kTorqueOffset;
  const uint32_t title = Load32(base, physics + torque_offset);
  PPCContext eval = ctx;
  eval.r3.u64 = physics;
  eval.f1.f64 = elapsed;
  __imp__sub_823021F0(eval, base);
  const double torque = LoadFloat(base, physics + torque_offset);
  std::memcpy(base + physics + kTorqueStateBegin, state, sizeof(state));
  for (uint32_t i = 0; i < 2; ++i) {
    std::memcpy(base + physics + kTyres + i * kTyreSize + kTyreStateBegin, tyres[i],
                sizeof(tyres[i]));
  }
  rex::memory::store_and_swap<uint32_t>(base + physics + torque_offset, title);
  return torque;
}

}  // namespace

// The steering torque's getter (r3 = the car's CCarDynamics): the control step's read names
// the car whose physics steps give the physics-rate force. With FM4_WRITE_WATCH_FILE it arms
// the write watch on the torque, which found its writer.
REX_HOOK_RAW(sub_828154B0) {
  static const bool watch = std::getenv("FM4_WRITE_WATCH_FILE") != nullptr;
  if (uint32_t(ctx.lr) == kTorqueRead) {
    if (watch) FM4WriteWatch(base, ctx.r3.u32 + kTorqueOffset);
    if (record_user == int(kWheelUser)) {
      player_physics.store(ctx.r3.u32 - kPhysicsFromDynamics, std::memory_order_relaxed);
    }
  }
  __imp__sub_828154B0(ctx, base);
}

// A car's step (r3 = its physics object, f1 = the frame's time step): its physics steps, then
// the torque model.
REX_HOOK_RAW(sub_82822BD0) {
  const uint32_t physics = ctx.r3.u32;
  const bool player = PhysicsOn() && physics &&
                      physics == player_physics.load(std::memory_order_relaxed) &&
                      player_driving.load(std::memory_order_relaxed);
  if (!player) {
    __imp__sub_82822BD0(ctx, base);
    return;
  }
  step_physics = physics;
  step_count = 0;
  step_elapsed = 0.0;
  step_last = 0.0;
  __imp__sub_82822BD0(ctx, base);
  step_physics = 0;
  if (Trace()) {
    TraceLine("phys %.2f u=%u steps=%d dt=%.6f last=%.6f title=%.6f", SteadyMs(),
              fm4_diagnostic_updates.load(std::memory_order_relaxed), step_count, step_elapsed,
              step_last, LoadFloat(base, physics + kPhysicsFromDynamics + kTorqueOffset));
  }
}

// The first part of a physics step (r3 = the physics object, f1 = the step's time).
REX_HOOK_RAW(sub_82304E80) {
  if (step_physics && ctx.r3.u32 == step_physics) step_dt = std::fabs(ctx.f1.f64);
  __imp__sub_82304E80(ctx, base);
}

// The last part of a physics step (r3 = the physics object): the torque on its state.
REX_HOOK_RAW(sub_82304ED8) {
  const uint32_t physics = ctx.r3.u32;
  __imp__sub_82304ED8(ctx, base);
  if (!step_physics || physics != step_physics) return;
  ++step_count;
  step_elapsed += step_dt;
  const double torque = EvaluateTorque(ctx, base, physics, step_elapsed);
  step_last = torque;
  const double clock = physics_clock.load(std::memory_order_relaxed) + step_dt;
  physics_clock.store(clock, std::memory_order_relaxed);
  // As sub_828C3330 scales the frame's torque: clamp(DynFFBScale x torque, +-min(DynFFBMaxForce,
  // 1)), then ForceFeedbackScale.
  const double limit = std::min(dynamic_max.load(std::memory_order_relaxed), 1.0);
  double force = std::isfinite(torque) ? dynamic_scale.load(std::memory_order_relaxed) * torque
                                       : 0.0;
  force = std::clamp(force, -limit, limit) * feedback_scale.load(std::memory_order_relaxed);
  rex::input::wheel::PublishForcePoint({clock, std::clamp(force, -1.0, 1.0), torque});
}
