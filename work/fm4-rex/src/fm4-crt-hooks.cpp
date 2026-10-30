// Native helpers for the title's C runtime on two hot paths
// (docs/analysis/career-season-soak.md, fm4-crt-helpers.h):
//
// - strstr (sub_82C88DA0): the host's strstr in place of the guest's byte loop,
//   which the first draw of a new car runs for its material names. FM4_NATIVE_STRSTR=0
//   keeps the guest's.
// - pow (sub_8230FCF0) for SetGammaRamp (sub_82873A30), which recomputes its 128
//   or 256 ramp entries with pow every frame, once more through sub_82857528 in
//   mode 4. At those three call sites a bounded memo returns the guest pow's own
//   earlier result for the same (base, exponent) bits, so the ramp stays the
//   guest's by construction. FM4_POW_MEMO=0 turns it off; FM4_POW_MEMO_CHECK=1 also
//   runs the guest pow on every hit and counts the results that differ.
//   FM4_GAMMA_RAMP_HASH=<file> writes the hash of every ramp SetGammaRamp hands on
//   (sub_826E0B88 with 3 x 256 u16, sub_826E0C58 with 3 x 128 pairs of u16: 1,536
//   bytes each, at r5), for an A/B of the memo.
//
// The recompiled pow touches no modelled FPSCR state but the flush mode, which
// its first floating-point instruction turns off (ctx.fpscr.disableFlushMode());
// the context models no sticky exception bits (rex/ppc/context.h), so a hit
// turns the flush mode off and sets f1, as the call would. Its other writes are
// to its own stack frame and, on the error paths that PowCacheable leaves out, the
// C runtime's error handling. Volatile registers are the caller's to lose.
#include "fm4-crt-helpers.h"

#include <rex/hook.h>

#include <atomic>
#include <bit>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>

namespace {

bool EnvOn(const char* name, bool fallback) {
  const char* value = std::getenv(name);
  if (!value || !*value) return fallback;
  return *value != '0';
}

const bool kNativeStrstr = EnvOn("FM4_NATIVE_STRSTR", true);
const bool kPowMemo = EnvOn("FM4_POW_MEMO", true);
const bool kPowMemoCheck = EnvOn("FM4_POW_MEMO_CHECK", false);

// SetGammaRamp's pow calls: their return addresses.
constexpr uint32_t kRampCallSites[] = {0x82873B10, 0x82873CC4, 0x82857560};

struct PowState {
  fm4::crt::PowMemo memo;
  std::atomic<std::thread::id> owner{};
  uint64_t mismatches = 0, other_threads = 0;
  bool reported_full = false;
};

PowState& Pow() {
  static PowState* state = [] {
    auto* result = new PowState;
    std::atexit([] {
      PowState& s = Pow();
      std::fprintf(stderr,
                   "FM4 pow memo: %llu lookups, %llu hits, %zu of %zu entries, %llu check mismatches, "
                   "%llu calls from other threads\n",
                   (unsigned long long)s.memo.lookups(), (unsigned long long)s.memo.hits(), s.memo.entries(),
                   s.memo.capacity(), (unsigned long long)s.mismatches, (unsigned long long)s.other_threads);
    });
    return result;
  }();
  return *state;
}

bool IsRampCall(uint32_t lr) {
  for (uint32_t site : kRampCallSites) {
    if (lr == site) return true;
  }
  return false;
}

// Guest addresses from 0xE0000000 are 0x1000 further in host memory (REX_PHYS_HOST_OFFSET):
// strings below it are host-contiguous.
constexpr uint32_t kHostOffsetStart = 0xE0000000u;

}  // namespace

REX_EXTERN(__imp__sub_82C88DA0);
REX_HOOK_RAW(sub_82C88DA0) {  // strstr
  const uint32_t haystack = ctx.r3.u32, needle = ctx.r4.u32;
  if (!kNativeStrstr || !haystack || !needle || haystack >= kHostOffsetStart - 0x100000u ||
      needle >= kHostOffsetStart - 0x100000u) {
    __imp__sub_82C88DA0(ctx, base);
    return;
  }
  ctx.r3.u64 = fm4::crt::Strstr(base, haystack, needle);
}

REX_EXTERN(__imp__sub_8230FCF0);
REX_HOOK_RAW(sub_8230FCF0) {  // pow
  if (!kPowMemo || !IsRampCall(uint32_t(ctx.lr))) {
    __imp__sub_8230FCF0(ctx, base);
    return;
  }
  PowState& state = Pow();
  // SetGammaRamp runs on the title's render thread: the memo belongs to the first thread that
  // calls it there; another thread calls the guest pow.
  std::thread::id expected{};
  const std::thread::id self = std::this_thread::get_id();
  if (!state.owner.compare_exchange_strong(expected, self) && expected != self) {
    ++state.other_threads;
    __imp__sub_8230FCF0(ctx, base);
    return;
  }
  const double x = ctx.f1.f64, y = ctx.f2.f64;
  const uint64_t x_bits = std::bit_cast<uint64_t>(x), y_bits = std::bit_cast<uint64_t>(y);
  uint64_t result = 0;
  if (state.memo.Find(x_bits, y_bits, &result)) {
    if (kPowMemoCheck) {
      __imp__sub_8230FCF0(ctx, base);
      if (ctx.f1.u64 != result) ++state.mismatches;
    }
    ctx.fpscr.disableFlushMode();
    ctx.f1.u64 = result;
    return;
  }
  __imp__sub_8230FCF0(ctx, base);
  if (fm4::crt::PowCacheable(x, y, ctx.f1.f64)) {
    state.memo.Insert(x_bits, y_bits, ctx.f1.u64);
    if (state.memo.full() && !state.reported_full) {
      state.reported_full = true;
      std::fprintf(stderr, "FM4 pow memo: full at %zu entries; later misses call the guest pow\n",
                   state.memo.entries());
    }
  }
}

namespace {

std::FILE* RampHashFile() {
  static std::FILE* file = [] () -> std::FILE* {
    const char* path = std::getenv("FM4_GAMMA_RAMP_HASH");
    return path && *path ? std::fopen(path, "w") : nullptr;
  }();
  return file;
}

void HashRamp(const char* name, uint32_t table, uint8_t* base) {
  std::FILE* file = RampHashFile();
  if (!file || !table || table >= kHostOffsetStart) return;
  static std::mutex mutex;
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now().time_since_epoch())
                      .count();
  const uint64_t hash = fm4::crt::Fnv1a(base + table, 1536);
  std::lock_guard<std::mutex> lock(mutex);
  std::fprintf(file, "%lld %s %016llx\n", (long long)ms, name, (unsigned long long)hash);
  std::fflush(file);
}

}  // namespace

REX_EXTERN(__imp__sub_826E0B88);
REX_HOOK_RAW(sub_826E0B88) {  // SetGammaRamp's 256-entry ramp: 3 x 256 u16 at r5
  HashRamp("ramp", ctx.r5.u32, base);
  __imp__sub_826E0B88(ctx, base);
}

REX_EXTERN(__imp__sub_826E0C58);
REX_HOOK_RAW(sub_826E0C58) {  // SetGammaRamp's 128-entry ramp: 3 x 128 (base, delta) u16 at r5
  HashRamp("pwl", ctx.r5.u32, base);
  __imp__sub_826E0C58(ctx, base);
}
