// The title's race clock, for the frozen-frame harness (docs/e2e/frozen-frames.md).
//
// Game::MainLoop (sub_822F7A00) runs one pass per frame on the title's main
// thread. Each pass takes the milliseconds since the last pass from its time
// source: a pass of 16 or 17 ms becomes a step of 0.01668335 s (1/59.94, with
// the rounding carried to the next pass), any other pass its measured length
// (more than 4,000 ms counts as 16). A pass that took 33 ms after a hitch
// therefore moves the race by 33 ms: the race follows wall time, at any frame
// rate. The developer option lockupdatetimeto60fps (CForzaCommandLineParameters
// +0x544) makes every pass a step of 0.01668335 s; FM4_FIXED_FRAME_TIME=1
// turns it on (fm4-command-line.cpp).
//
// The step reaches the session update (sub_8240DB08: session, step), which
// scales it (the scale at +0x20, -1 while the rewind runs back, an override at
// +0x24) and adds it to the session time, a double at +0x18, before it updates
// the session's components. The session time counts from the pre-race and
// stops while the rewind is shown.
//
// The diagnostic controller's state polls are not one per pass: now and then a
// pass polls twice or not at all (the controller is polled on the main thread,
// but not in every pass), so a race frame counted in polls falls on another pass,
// and another race time, from run to run. This hook counts the session updates
// (fm4_diagnostic_updates) and keeps the last session time; the push of a race
// render scenario records the count (fm4-vblank-rate.cpp), and FM4TestInput
// counts race frames in session updates from it.
//
// The countdown (C321Component, update sub_8239D450) places the cars on the
// grid (its +0x1C flag sets; the push of the N_321_and_PostRace scenario),
// then waits until the race is ready (the session's flags at +353, +355 and
// +356, set as the loading ends) before its 3-2-1 starts (its +0x18 flag
// clears). From there the 3-2-1 runs on the session time: the race scenario
// follows 8.49 s of session time later (509 session updates with the fixed
// step below, about 1,000 with the measured one, as the countdown runs two
// passes per presented frame). The wait follows the loading (29 to 40 updates
// in Quick Races with the fixed step on the same machine), and the cars stand
// on the grid during it, so they start the 3-2-1 in other states, which the
// revving before the start makes larger. With FM4_FIXED_FRAME_TIME=1 the
// countdown sees the first flag (+353, sub_826D32D8) clear until
// kCountdownWait session updates after it placed the cars, so the 3-2-1
// starts on the same update whenever the loading ends before that (a line
// says when it does not). The 3-2-1 start also resets the cars' random states
// with FM4_FIXED_SEED (fm4-fixed-seed.cpp).
//
// With both switches, dumps of the 12 cars' dynamics (CCarDynamics) in two
// runs (21 October 2026, a probe build) showed no new difference from 30 to
// 324 session updates after the placing of the cars (264 into the 3-2-1),
// beside 156 words that already differed 30 updates after it. On the 325th,
// while the AI revs the engines, 11 cars differ at once, from the last bit of
// car 1's +0x14 to per-wheel fields; thresholds of the engine code (+0x17F0)
// then amplify it, and the race no longer repeats. The input that differs is
// not found (docs/e2e/frozen-frames.md).
#include <rex/hook.h>
#include <rex/memory.h>
#include "fm4-diagnostic-state.h"
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

void FM4FixedSeedCountdownStart();

namespace {
// Session updates between placing the cars and the 3-2-1 with FM4_FIXED_FRAME_TIME.
constexpr uint32_t kCountdownWait = 60;
// sub_8239D450's return address from reading the first ready flag (+353).
constexpr uint32_t kCountdownReadyReturn = 0x8239D71C;

bool FixedFrameTime() {
  static const bool fixed = []() {
    const char* value = std::getenv("FM4_FIXED_FRAME_TIME");
    return value && std::strcmp(value, "1") == 0;
  }();
  return fixed;
}

// The session update on which the countdown placed the cars; 0 before.
std::atomic<uint32_t> countdown_placed{0};
}  // namespace

// Session update (session, f1 = step).
REX_EXTERN(__imp__sub_8240DB08);
REX_HOOK_RAW(sub_8240DB08) {
  const uint32_t session = ctx.r3.u32;
  __imp__sub_8240DB08(ctx, base);
  const uint64_t time = rex::memory::load_and_swap<uint64_t>(base + session + 0x18);
  fm4_diagnostic_session_time.store(std::bit_cast<double>(time), std::memory_order_relaxed);
  fm4_diagnostic_updates.fetch_add(1, std::memory_order_relaxed);
}

// C321Component update (this, f1 = step).
REX_EXTERN(__imp__sub_8239D450);
REX_HOOK_RAW(sub_8239D450) {
  const uint32_t self = ctx.r3.u32;
  const bool placed = base[self + 0x1C] != 0;
  const bool waiting = base[self + 0x18] != 0;
  __imp__sub_8239D450(ctx, base);
  const uint32_t update = fm4_diagnostic_updates.load(std::memory_order_relaxed);
  if (!placed && base[self + 0x1C]) {
    countdown_placed.store(update, std::memory_order_relaxed);
    FM4TestEvent("countdown placed");
  }
  if (waiting && !base[self + 0x18]) {
    const uint32_t waited = update - countdown_placed.load(std::memory_order_relaxed);
    FM4TestEvent("countdown start %u", waited);
    if (FixedFrameTime())
      std::fprintf(stderr, "FM4 fixed frame time: the 3-2-1 starts %u session updates after the "
                   "cars were placed%s\n", waited,
                   waited == kCountdownWait ? "" : " (the race was not ready in time: not pinned)");
    FM4FixedSeedCountdownStart();
  }
}

// The session's first ready flag (+353; session).
REX_EXTERN(__imp__sub_826D32D8);
REX_HOOK_RAW(sub_826D32D8) {
  __imp__sub_826D32D8(ctx, base);
  if (uint32_t(ctx.lr) == kCountdownReadyReturn && FixedFrameTime()) {
    const uint32_t placed = countdown_placed.load(std::memory_order_relaxed);
    if (placed && fm4_diagnostic_updates.load(std::memory_order_relaxed) - placed < kCountdownWait)
      ctx.r3.u64 = 0;
  }
}
