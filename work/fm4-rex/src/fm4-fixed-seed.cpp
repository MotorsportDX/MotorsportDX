// Fixed seed for the race setup (FM4_FIXED_SEED=<n>, decimal or 0x hex, off by
// default): the first Quick Race of each process then starts the same way, with
// the same opponent cars and grid. Two guest random sources make a race setup
// differ between runs:
//   - The opponent cars: the title picks them with SQL (SELECT ... FROM
//     Drivable_Data_Car ... ORDER BY RANDOM() LIMIT %d, sub_824885C0). SQLite's
//     random() draws from its RC4 generator (state at 0x82E67848: isInit, i, j,
//     s[256]), keyed once from the VFS xRandomness. The grid follows from the
//     cars (the player's slot too). With the switch, the generator is keyed
//     from the seed at the start of each opponent query.
//   - The AI drivers: each AI driver (sub_82A92FF0) seeds its random state
//     (driver+124, a copy at +128) from sub_8234BD70, which returns
//     [[0x820008E8]+16], presumably the tick count (not verified). With the
//     switch, the n-th driver of a race gets a seed derived from the seed and n.
//   - The cars: each car's dynamics (CCarDynamics, constructor sub_828221D0)
//     keeps a random state at +0x41D0, set to 12345678 when the car is built
//     and drawn on every session update (sub_82304870, sub_8230CAA8). The
//     cars are built in the pre-race, whose length follows the wall clock
//     (the intro, and the countdown waiting until the race is ready), so the
//     states differ by the start of the 3-2-1. With the switch, the
//     countdown (fm4-race-clock.cpp) resets every car's state to 12345678
//     when its 3-2-1 starts: the next draw from a car state after that point
//     (the same update for every car) first stores the initial value, so
//     only states in use are written.
// The title's other random states (the ANSI C LCG: state * 1103515245 + 12345)
// are set to 12345678 when their objects are built and are not reseeded. The
// crowd (CCrowdPresentation::Update, sub_82948920) draws from one of them as
// often as it renders, so the crowd differs between runs.
//
// Diagnostic (FM4_TRACE_RNG=1): the first draws of each caller of the LCG
// functions (sub_828C8910 next, sub_828C8930 int range, sub_828C89A8 vector,
// sub_82303E60 float 0..1, sub_823866D0 float range) with the state address and
// value, each sqlite3_randomness call, the opponent queries, and a summary of
// the LCG callers at each session start.
#include <rex/hook.h>
#include <rex/memory.h>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <optional>
#include <tuple>

namespace {
// sqlite3Prng: isInit, i, j, then s[256].
constexpr uint32_t kSqlitePrng = 0x82E67848;
// sub_82A92FF0's return address from seeding the AI driver's random state.
constexpr uint32_t kAiDriverSeedReturn = 0x82A930B8;
// sub_828221D0's return address from setting the car's random state (+0x41D0).
constexpr uint32_t kCarSeedReturn = 0x82822278;
constexpr uint32_t kCarSeed = 12345678;

std::optional<uint64_t> FixedSeed() {
  static const std::optional<uint64_t> seed = []() -> std::optional<uint64_t> {
    const char* value = std::getenv("FM4_FIXED_SEED");
    if (!value || !*value) return std::nullopt;
    // Decimal, or hex with 0x (base 0 would read a leading 0 as octal).
    const bool hex = value[0] == '0' && (value[1] == 'x' || value[1] == 'X');
    const char* digits = hex ? value + 2 : value;
    char* end = nullptr;
    const uint64_t seed = std::strtoull(digits, &end, hex ? 16 : 10);
    if (!std::isxdigit(static_cast<unsigned char>(*digits)) || *end) {
      std::fprintf(stderr, "FM4 fixed seed: ignoring FM4_FIXED_SEED=%s (not a number)\n", value);
      return std::nullopt;
    }
    std::fprintf(stderr, "FM4 fixed seed %llu: opponent cars, grid and AI driver seeds\n",
                 static_cast<unsigned long long>(seed));
    return seed;
  }();
  return seed;
}

uint64_t SplitMix64(uint64_t& state) {
  uint64_t z = (state += 0x9E3779B97F4A7C15ull);
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  return z ^ (z >> 31);
}

// The AI drivers created since the last opponent query or session start.
uint32_t ai_drivers = 0;

// The cars' random states (address: the countdown start they were last reset for).
std::mutex car_mutex;
std::map<uint32_t, uint32_t> car_states;
uint32_t countdown_starts = 0;

// Before a draw from state: resets a car's state once per countdown start.
void ResetCarState(uint8_t* base, uint32_t state) {
  if (!countdown_starts) return;
  std::lock_guard lock(car_mutex);
  const auto it = car_states.find(state);
  if (it == car_states.end() || it->second == countdown_starts) return;
  it->second = countdown_starts;
  rex::memory::store_and_swap<uint32_t>(base + state, kCarSeed);
}

// Keys SQLite's RC4 generator as its first use does, with key bytes from the seed.
void KeySqliteGenerator(uint8_t* base, uint64_t seed) {
  uint8_t key[256];
  uint64_t state = seed;
  for (uint32_t i = 0; i < 256; i += 8) {
    const uint64_t word = SplitMix64(state);
    for (uint32_t b = 0; b < 8; ++b) key[i + b] = uint8_t(word >> (b * 8));
  }
  uint8_t* prng = base + kSqlitePrng;
  uint8_t* s = prng + 3;
  for (uint32_t i = 0; i < 256; ++i) s[i] = uint8_t(i);
  uint8_t j = 0;
  for (uint32_t i = 0; i < 256; ++i) {
    j = uint8_t(j + s[i] + key[i]);
    const uint8_t t = s[j];
    s[j] = s[i];
    s[i] = t;
  }
  prng[0] = 1;  // isInit
  prng[1] = 0;  // i
  prng[2] = 0;  // j
}

bool TraceEnabled() {
  static const bool enabled = std::getenv("FM4_TRACE_RNG") != nullptr;
  return enabled;
}

struct Site {
  uint64_t draws = 0;
  uint32_t first_state = 0;
  uint64_t first_seq = 0;
};
std::mutex trace_mutex;
std::map<std::tuple<const char*, uint32_t, uint32_t>, Site> sites;  // (function, caller, state)
uint64_t sequence = 0;
uint32_t session = 0;

void Trace(const uint8_t* base, const char* function, uint32_t state, uint32_t caller) {
  if (!TraceEnabled()) return;
  const uint32_t value = rex::memory::load_and_swap<uint32_t>(base + state);
  std::lock_guard lock(trace_mutex);
  Site& site = sites[{function, caller, state}];
  if (!site.draws++) {
    site.first_state = value;
    site.first_seq = sequence;
  }
  if (site.draws <= 3)
    std::fprintf(stderr, "FM4 rng #%llu session %u %s state 0x%08X = 0x%08X caller %08X\n",
                 static_cast<unsigned long long>(sequence), session, function, state, value,
                 caller);
  ++sequence;
}
}  // namespace

// Called by the session start hook (sub_826CFA68, fm4-dev-features.cpp).
void FM4FixedSeedSessionStart(uint8_t* base) {
  (void)base;
  ai_drivers = 0;
  if (!TraceEnabled()) return;
  std::lock_guard lock(trace_mutex);
  std::fprintf(stderr, "FM4 rng summary at session %u start, draw #%llu:\n", session,
               static_cast<unsigned long long>(sequence));
  for (const auto& [key, site] : sites)
    std::fprintf(stderr, "FM4 rng   %s caller %08X state 0x%08X: %llu draws, first #%llu 0x%08X\n",
                 std::get<0>(key), std::get<1>(key), std::get<2>(key),
                 static_cast<unsigned long long>(site.draws),
                 static_cast<unsigned long long>(site.first_seq), site.first_state);
  ++session;
}

// The AI opponents of a race (this, ...): SELECT ... FROM Drivable_Data_Car ...
// ORDER BY RANDOM() LIMIT %d.
REX_EXTERN(__imp__sub_824885C0);
REX_HOOK_RAW(sub_824885C0) {
  if (TraceEnabled())
    std::fprintf(stderr, "FM4 rng #%llu opponent cars query, caller %08X\n",
                 static_cast<unsigned long long>(sequence), uint32_t(ctx.lr));
  if (const auto seed = FixedSeed()) {
    KeySqliteGenerator(base, *seed);
    ai_drivers = 0;
  }
  __imp__sub_824885C0(ctx, base);
}

// The opponents' paint (Combo_Colors ... ORDER BY random()); not reached in a
// Quick Race, traced to show when it is.
REX_EXTERN(__imp__sub_824882B8);
REX_HOOK_RAW(sub_824882B8) {
  if (TraceEnabled())
    std::fprintf(stderr, "FM4 rng #%llu opponent paint query, caller %08X\n",
                 static_cast<unsigned long long>(sequence), uint32_t(ctx.lr));
  __imp__sub_824882B8(ctx, base);
}

// Sets the state of a random number object (object, seed).
REX_EXTERN(__imp__sub_822B9898);
REX_HOOK_RAW(sub_822B9898) {
  if (uint32_t(ctx.lr) == kCarSeedReturn && FixedSeed()) {
    std::lock_guard lock(car_mutex);
    car_states[ctx.r3.u32] = countdown_starts;
  }
  if (uint32_t(ctx.lr) == kAiDriverSeedReturn) {
    if (const auto seed = FixedSeed()) {
      uint64_t state = *seed ^ (uint64_t(ai_drivers++) << 32);
      const uint32_t pinned = uint32_t(SplitMix64(state));
      // r31 is the driver (sub_82A92FF0), whose +128 copy of the seed was just stored.
      rex::memory::store_and_swap<uint32_t>(base + ctx.r31.u32 + 128, pinned);
      ctx.r4.u32 = pinned;
    }
  }
  __imp__sub_822B9898(ctx, base);
}

// sqlite3_randomness (bytes, buffer).
REX_EXTERN(__imp__sub_82BCED10);
REX_HOOK_RAW(sub_82BCED10) {
  if (TraceEnabled()) {
    std::lock_guard lock(trace_mutex);
    std::fprintf(stderr, "FM4 rng #%llu session %u sqlite %u bytes, init %u i %u j %u caller %08X\n",
                 static_cast<unsigned long long>(sequence++), session, ctx.r3.u32,
                 base[kSqlitePrng], base[kSqlitePrng + 1], base[kSqlitePrng + 2],
                 uint32_t(ctx.lr));
  }
  __imp__sub_82BCED10(ctx, base);
}

REX_EXTERN(__imp__sub_828C8910);
REX_HOOK_RAW(sub_828C8910) {
  Trace(base, "next", ctx.r3.u32, uint32_t(ctx.lr));
  __imp__sub_828C8910(ctx, base);
}

REX_EXTERN(__imp__sub_828C8930);
REX_HOOK_RAW(sub_828C8930) {
  Trace(base, "int", ctx.r3.u32, uint32_t(ctx.lr));
  __imp__sub_828C8930(ctx, base);
}

REX_EXTERN(__imp__sub_828C89A8);
REX_HOOK_RAW(sub_828C89A8) {
  Trace(base, "vector", ctx.r4.u32, uint32_t(ctx.lr));
  __imp__sub_828C89A8(ctx, base);
}

REX_EXTERN(__imp__sub_82303E60);
REX_HOOK_RAW(sub_82303E60) {
  ResetCarState(base, ctx.r3.u32);
  Trace(base, "unit", ctx.r3.u32, uint32_t(ctx.lr));
  __imp__sub_82303E60(ctx, base);
}

REX_EXTERN(__imp__sub_823866D0);
REX_HOOK_RAW(sub_823866D0) {
  ResetCarState(base, ctx.r3.u32);
  Trace(base, "float", ctx.r3.u32, uint32_t(ctx.lr));
  __imp__sub_823866D0(ctx, base);
}

// Called by the countdown hook (fm4-race-clock.cpp) when the 3-2-1 starts.
void FM4FixedSeedCountdownStart() {
  if (!FixedSeed()) return;
  std::lock_guard lock(car_mutex);
  ++countdown_starts;
  std::fprintf(stderr, "FM4 fixed seed: the 3-2-1 starts, %zu car random states reset to %u\n",
               car_states.size(), kCarSeed);
}
