#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <rex/hook.h>
#include <rex/input/device.h>
#include <rex/input/title_input_ranges.h>
#include <array>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include "fm4-diagnostic-state.h"

REX_EXTERN(__imp__sub_825D8850);
REX_EXTERN(__imp__sub_825D5C18);
REX_EXTERN(__imp__sub_825D7738);
REX_EXTERN(__imp__sub_82516C38);
namespace {
uint32_t UIRead(uint8_t* base, uint32_t address) {
  uint32_t value = 0; SIZE_T size = 0;
  if (!ReadProcessMemory(GetCurrentProcess(), base + address, &value, 4, &size) || size != 4) return 0;
  return _byteswap_ulong(value);
}
void TraceUI(PPCContext& ctx, uint8_t* base, PPCFunc* original, uint32_t function) {
  if (std::getenv("FM4_CAPTURE_DIR")) {
    const auto object = ctx.r3.u32;
    const auto vtable = UIRead(base, object);
    static thread_local unsigned calls = 0;
    if ((function != 0x82516C38 || vtable == 0x82043E04) &&
        ((fm4_diagnostic_buttons.load(std::memory_order_relaxed) & 0x1003) || calls < 8) && calls < 600) {
      ++calls;
      std::fprintf(stderr, "FM4 UI fn=%08x n=%u object=%08x vt=%08x r4=%08x r5=%08x r6=%08x r7=%08x lr=%llx state832=%08x state836=%08x control=%08x\n",
        function, calls, object, vtable, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32, ctx.lr,
        UIRead(base, object + 832), UIRead(base, object + 836), UIRead(base, object + 864));
      std::fflush(stderr);
    }
  }
  original(ctx, base);
}
}
REX_HOOK_RAW(sub_825D8850) { TraceUI(ctx, base, __imp__sub_825D8850, 0x825D8850); }
REX_HOOK_RAW(sub_825D5C18) { TraceUI(ctx, base, __imp__sub_825D5C18, 0x825D5C18); }
REX_HOOK_RAW(sub_825D7738) { TraceUI(ctx, base, __imp__sub_825D7738, 0x825D7738); }
REX_HOOK_RAW(sub_82516C38) { TraceUI(ctx, base, __imp__sub_82516C38, 0x82516C38); }

// Observe the game's actual controller reads and converted button state.
// Bounded and opt-in; neither hook changes guest registers or memory.
REX_EXTERN(__imp__sub_82C778C8);
REX_HOOK_RAW(sub_82C778C8) {
  const auto user = ctx.r3.u32, address = ctx.r4.u32;
  const auto caller = uint32_t(ctx.lr);
  __imp__sub_82C778C8(ctx, base);
  if (!std::getenv("FM4_TEST_INPUT_FILE") || ctx.r3.u32 || !address) return;
  const auto buttons = UIRead(base, address + 4) >> 16;
  static thread_local std::map<uint64_t, uint32_t> previous;
  static thread_local unsigned count = 0;
  const uint64_t key = (uint64_t(caller) << 32) | user;
  const auto found = previous.find(key);
  if ((found == previous.end() || found->second != buttons) && count++ < 256) {
    std::fprintf(stderr, "FM4 guest input: caller=%08x user=%u packet=%u buttons=%04x state=%08x\n",
                 caller, user, UIRead(base, address), buttons, address);
    std::fflush(stderr);
  }
  previous[key] = buttons;
}

// FM4_INPUT_TRACE_FILE=<path>: the title's own stick, trigger and rumble handling, one line
// per change (docs/analysis/modern-gamepads.md). Observes only, bounded, nothing without it.
//
//   cfg <steady ms> object=<o> <ten floats>   the deadzones sub_822B4130 gets in f1-f10:
//       left stick inner, outer; right stick inner, outer; left trigger inner, outer;
//       right trigger inner, outer; the right stick's second pair (or 0, 0)
//   in <steady ms> u=<updates> r=<race update> object=<o> raw lx ly rx ry lt rt buttons
//       out lt rt L dir angle magnitude x y R dir angle magnitude x y
//   rumble <steady ms> u=<updates> r=<race update> user=<n> left=<0..65535> right=<0..65535>
//       the title's XInputSetState (sub_822A8F38); rumble-async for its other path
//       (sub_828C50E8, the overlapped one)
namespace {
FILE* InputTrace() {
  static FILE* file = []() -> FILE* {
    const char* path = std::getenv("FM4_INPUT_TRACE_FILE");
    if (!path || !*path) return nullptr;
    FILE* opened = std::fopen(path, "w");
    if (!opened) std::fprintf(stderr, "FM4 input trace: cannot open %s\n", path);
    return opened;
  }();
  return file;
}
std::mutex input_trace_mutex;
unsigned input_trace_lines = 0;
constexpr unsigned kInputTraceLimit = 200000;
double SteadyMs() {
  using namespace std::chrono;
  return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}
void InputTraceLine(const char* format, ...) {
  FILE* file = InputTrace();
  if (!file) return;
  std::lock_guard lock(input_trace_mutex);
  if (input_trace_lines >= kInputTraceLimit) return;
  ++input_trace_lines;
  va_list args;
  va_start(args, format);
  std::vfprintf(file, format, args);
  va_end(args);
  std::fputc('\n', file);
  if (input_trace_lines % 256 == 0) std::fflush(file);
}
void TraceVibration(bool async, uint32_t user, uint8_t* base, uint32_t vibration) {
  if (!InputTrace() || !vibration) return;
  const uint32_t motors = UIRead(base, vibration);
  static std::mutex mutex;
  static std::map<uint64_t, uint32_t> previous;
  {
    std::lock_guard lock(mutex);
    const uint64_t key = (uint64_t(async) << 32) | user;
    const auto found = previous.find(key);
    if (found != previous.end() && found->second == motors) return;
    previous[key] = motors;
  }
  InputTraceLine("%s %.2f u=%u r=%u user=%u left=%u right=%u", async ? "rumble-async" : "rumble",
                 SteadyMs(),
                 fm4_diagnostic_updates.load(std::memory_order_relaxed),
                 fm4_diagnostic_race_update.load(std::memory_order_relaxed), user, motors >> 16,
                 motors & 0xFFFF);
}
// The keyboard's deadzone compensation (docs/analysis/keyboard-controls.md). The input poll
// sub_822B5E60 calls the conversion once per user per frame, returning to 0x822B5F6C, with the
// user index in r27 and the profile's deadzones in f1-f8 in the pad's raw units: left stick
// inner and outer, right stick inner and outer, left trigger inner and outer, right trigger
// inner and outer. They are copied for the runtime's keyboard driver; the conversion runs as
// it would without the hook.
void PublishInputRanges(const PPCContext& ctx) {
  if (ctx.lr != 0x822B5F6C || ctx.r27.u32 >= rex::input::kMaxGuestUsers) return;
  rex::input::keyboard::TitleRanges ranges;
  ranges.steering = {ctx.f1.f64, ctx.f2.f64};
  ranges.left_trigger = {ctx.f5.f64, ctx.f6.f64};
  ranges.right_trigger = {ctx.f7.f64, ctx.f8.f64};
  rex::input::SetTitleInputRanges(ctx.r27.u32, ranges);
}
}  // namespace

// Until the title has converted a user's input: the profile's defaults (ForzaProfile.sch,
// ControllerAdvancedOptions: steering 0.24 to 0.95, brake 0.15 to 0.95, throttle 0.15 to 0.90).
void FM4InstallInputRanges() {
  rex::input::keyboard::TitleRanges ranges;
  ranges.steering = {0.24 * 32767.0, 0.95 * 32767.0};
  ranges.left_trigger = {0.15 * 255.0, 0.95 * 255.0};
  ranges.right_trigger = {0.15 * 255.0, 0.90 * 255.0};
  rex::input::SetDefaultTitleInputRanges(ranges);
}

REX_EXTERN(__imp__sub_822B4130);
REX_HOOK_RAW(sub_822B4130) {
  PublishInputRanges(ctx);
  const auto object = ctx.r3.u32, input = ctx.r4.u32;
  const auto buttons = std::getenv("FM4_TEST_INPUT_FILE") ? UIRead(base, input + 4) >> 16 : 0;
  if (InputTrace()) {
    const double config[10] = {ctx.f1.f64, ctx.f2.f64, ctx.f3.f64, ctx.f4.f64, ctx.f5.f64,
                               ctx.f6.f64, ctx.f7.f64, ctx.f8.f64, ctx.f9.f64, ctx.f10.f64};
    // X_INPUT_STATE: packet, buttons and triggers, the four sticks.
    const uint32_t raw[4] = {UIRead(base, input + 4), UIRead(base, input + 8),
                             UIRead(base, input + 12), 0};
    __imp__sub_822B4130(ctx, base);
    static thread_local std::map<uint32_t, std::array<double, 10>> configs;
    auto& known = configs[object];
    if (std::memcmp(known.data(), config, sizeof(config)) != 0) {
      std::memcpy(known.data(), config, sizeof(config));
      InputTraceLine("cfg %.2f object=%08x %g %g %g %g %g %g %g %g %g %g", SteadyMs(), object,
                     config[0], config[1], config[2], config[3], config[4], config[5], config[6],
                     config[7], config[8], config[9]);
    }
    uint32_t out[11] = {UIRead(base, object)};
    for (unsigned i = 0; i < 5; ++i) {
      out[1 + i] = UIRead(base, object + 36 + 4 * i);
      out[6 + i] = UIRead(base, object + 56 + 4 * i);
    }
    static thread_local std::map<uint32_t, std::array<uint32_t, 14>> states;
    std::array<uint32_t, 14> state;
    std::memcpy(state.data(), raw, 3 * sizeof(uint32_t));
    std::memcpy(state.data() + 3, out, sizeof(out));
    auto& last = states[object];
    if (last != state) {
      last = state;
      auto f = [&](unsigned i) {
        float value;
        std::memcpy(&value, &out[i], sizeof(value));
        return double(value);
      };
      InputTraceLine(
          "in %.2f u=%u r=%u object=%08x raw %d %d %d %d %u %u %04x out %u %u L %u %.4f %.4f %.4f "
          "%.4f R %u %.4f %.4f %.4f %.4f",
          SteadyMs(), fm4_diagnostic_updates.load(std::memory_order_relaxed),
          fm4_diagnostic_race_update.load(std::memory_order_relaxed), object,
          int16_t(raw[1] >> 16), int16_t(raw[1]), int16_t(raw[2] >> 16), int16_t(raw[2]),
          (raw[0] >> 8) & 0xFF, raw[0] & 0xFF, raw[0] >> 16, out[0] >> 24, (out[0] >> 16) & 0xFF,
          out[1], f(2), f(3), f(4), f(5), out[6], f(7), f(8), f(9), f(10));
    }
  } else {
    __imp__sub_822B4130(ctx, base);
  }
  if (!std::getenv("FM4_TEST_INPUT_FILE")) return;
  static thread_local std::map<uint32_t, uint32_t> previous;
  static thread_local unsigned count = 0;
  auto found = previous.find(object);
  if ((found == previous.end() || found->second != buttons) && count++ < 256) {
    std::fprintf(stderr, "FM4 converted input: object=%08x buttons=%04x words=", object, buttons);
    for (unsigned offset=0; offset<32; offset+=4)
      std::fprintf(stderr, "%08x,", UIRead(base, object + offset));
    std::fprintf(stderr, "\n");
    std::fflush(stderr);
  }
  previous[object] = buttons;
}

// Observe current, previous and release-history masks in the input owner.
// The generated caller copies owner+64 to owner+248 before converting the
// new controller state. This hook is diagnostic only and bounded per thread.
REX_EXTERN(__imp__sub_82379300);
REX_HOOK_RAW(sub_82379300) {
  const auto object = ctx.r3.u32;
  static const bool enabled = std::getenv("FM4_INPUT_EDGE_TRACE") != nullptr;
  static thread_local unsigned count = 0;
  static thread_local std::map<uint32_t, uint64_t> previous;
  const auto mask = enabled ? (uint64_t(UIRead(base, object + 72)) << 32 |
                               UIRead(base, object + 76)) : 0;
  __imp__sub_82379300(ctx, base);
  if (!enabled || count >= 128) return;
  const auto found = previous.find(object);
  if (found == previous.end() || found->second != mask) {
    ++count;
    std::fprintf(stderr, "FM4 input edge: thread=%lu owner=%08x current=%016llx previous=%08x%08x history=%08x%08x\n",
      GetCurrentThreadId(), object, static_cast<unsigned long long>(mask),
      UIRead(base, object + 256), UIRead(base, object + 260),
      UIRead(base, object + 440), UIRead(base, object + 444));
    std::fflush(stderr);
  }
  previous[object] = mask;
}

// The title's XInputSetState (user, vibration): the XDK wrapper that swaps the motors for an
// old wheel and calls XamInputSetState. The input poll (sub_822B5E60) calls it once per pad
// and poll with the motors of its rumble mixer, or zero when rumble is off.
REX_EXTERN(__imp__sub_822A8F38);
REX_HOOK_RAW(sub_822A8F38) {
  const auto user = ctx.r3.u32, vibration = ctx.r4.u32;
  TraceVibration(false, user, base, vibration);
  __imp__sub_822A8F38(ctx, base);
}

// The poll's other path (object, vibration), taken when the pad's object has its flag at
// +4: an overlapped set-state call that is retried until it completes.
REX_EXTERN(__imp__sub_828C50E8);
REX_HOOK_RAW(sub_828C50E8) {
  const auto object = ctx.r3.u32, vibration = ctx.r4.u32;
  TraceVibration(true, object, base, vibration);
  __imp__sub_828C50E8(ctx, base);
}
