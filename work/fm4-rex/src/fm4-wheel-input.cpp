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
