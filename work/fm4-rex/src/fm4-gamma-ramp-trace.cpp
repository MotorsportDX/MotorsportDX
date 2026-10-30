// FM4_GAMMA_RAMP_HASH=<file>: the hash of every gamma ramp the title's
// SetGammaRamp (sub_82873A30) hands to its D3D layer, one line each
// (docs/e2e/traces.md): "<steady clock ms> ramp|pwl <FNV-1a 64>", for the
// 256-entry ramp (sub_826E0B88, 3 x 256 u16) and the 128-entry piecewise linear
// one (sub_826E0C58, 3 x 128 pairs of u16), 1,536 bytes each at r5. The title's
// render thread recomputes the ramp on every turn of its frame loop
// (docs/analysis/career-season-soak.md, section 11), so the lines show how often
// that loop turns, and two runs that should give the same picture must give the
// same hashes. Off without the variable.
#include <rex/hook.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>

namespace {

// Guest addresses from 0xE0000000 are 0x1000 further in host memory
// (REX_PHYS_HOST_OFFSET): a table below it is host-contiguous.
constexpr uint32_t kHostOffsetStart = 0xE0000000u;
constexpr uint32_t kRampBytes = 1536;

uint64_t Fnv1a(const uint8_t* data, size_t size) {
  uint64_t hash = 0xCBF29CE484222325ull;
  for (size_t i = 0; i < size; ++i) {
    hash = (hash ^ data[i]) * 0x100000001B3ull;
  }
  return hash;
}

std::FILE* RampHashFile() {
  static std::FILE* file = []() -> std::FILE* {
    const char* path = std::getenv("FM4_GAMMA_RAMP_HASH");
    return path && *path ? std::fopen(path, "w") : nullptr;
  }();
  return file;
}

void HashRamp(const char* name, uint32_t table, uint8_t* base) {
  std::FILE* file = RampHashFile();
  if (!file || !table || table >= kHostOffsetStart - kRampBytes) return;
  static std::mutex mutex;
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now().time_since_epoch())
                      .count();
  const uint64_t hash = Fnv1a(base + table, kRampBytes);
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
