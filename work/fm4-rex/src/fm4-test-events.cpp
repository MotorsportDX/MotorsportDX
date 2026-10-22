// Screen events for the e2e harness (docs/e2e/README.md, "Fast route"). With
// FM4_TEST_EVENTS_FILE=<path> the title appends one line per event to that file:
//
//   <unix ms> <steady ms> <kind> <detail>
//
// The unix time is the wall clock that e2e.py's timeline uses; the steady time is
// the clock of the other traces (captures.txt, n0.jsonl). The kinds:
//
//   scene <id> <name>  the frontend created a UI scene (a screen, a dialog, the
//                      HUD), with its id and name in the scene registry;
//   push <name>        a render scenario was pushed (fm4-vblank-rate.cpp): the
//                      pre-race, the countdown (N_321_and_PostRace), the race
//                      (*_Race), photo mode, replays;
//   pop <name>         the topmost render scenario was popped;
//   countdown placed   the countdown placed the cars on the grid (fm4-race-clock.cpp);
//   countdown start <n>  its 3-2-1 starts, n session updates after the placing.
//
// e2e.py waits for these instead of fixed delays or OCR where a screen has an
// event (checked steps whose text starts with '@'). The writer only observes:
// no guest register or memory changes, and nothing is written without the
// variable. Events are rare (a few hundred per run), so each line is flushed.
//
// The frontend's scene registry (built by sub_824E2790) holds 249 entries of
// (factory, name) from +4; sub_824B8EE0(registry, id, parent) calls the factory
// of entry id and stores the id at +12 of the new scene (docs/analysis/
// direct-race-launch.md, "What GO RACE does").
#include <rex/hook.h>
#include <rex/memory.h>
#include "fm4-diagnostic-state.h"
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>

namespace {
constexpr uint32_t kSceneCount = 249;

std::mutex mutex;

FILE* EventFile() {
  static FILE* file = []() -> FILE* {
    const char* path = std::getenv("FM4_TEST_EVENTS_FILE");
    if (!path || !*path) return nullptr;
    FILE* opened = std::fopen(path, "a");
    if (!opened) std::fprintf(stderr, "FM4 test events: cannot open %s\n", path);
    return opened;
  }();
  return file;
}

std::string GuestString(const uint8_t* base, uint32_t address, size_t limit) {
  std::string text;
  if (!address) return "?";
  for (size_t i = 0; i < limit && base[address + i]; ++i) {
    const char c = static_cast<char>(base[address + i]);
    text.push_back(c > ' ' && c < 127 ? c : '_');
  }
  return text.empty() ? "?" : text;
}
}  // namespace

bool FM4TestEventsOn() { return EventFile() != nullptr; }

void FM4TestEvent(const char* format, ...) {
  FILE* file = EventFile();
  if (!file) return;
  using namespace std::chrono;
  const auto unix_ms =
      duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
  const auto steady_ms =
      duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
  std::lock_guard lock(mutex);
  std::fprintf(file, "%lld %lld ", static_cast<long long>(unix_ms),
               static_cast<long long>(steady_ms));
  va_list args;
  va_start(args, format);
  std::vfprintf(file, format, args);
  va_end(args);
  std::fputc('\n', file);
  std::fflush(file);
}

// The UI scene factory (registry, id, parent): returns the new scene.
REX_EXTERN(__imp__sub_824B8EE0);
REX_HOOK_RAW(sub_824B8EE0) {
  const uint32_t registry = ctx.r3.u32;
  const uint32_t id = ctx.r4.u32 & 0xFFFF;
  __imp__sub_824B8EE0(ctx, base);
  if (!FM4TestEventsOn() || id >= kSceneCount) return;
  const uint32_t name = rex::memory::load_and_swap<uint32_t>(base + registry + 8 + id * 8);
  FM4TestEvent("scene %u %s", id, GuestString(base, name, 64).c_str());
}
