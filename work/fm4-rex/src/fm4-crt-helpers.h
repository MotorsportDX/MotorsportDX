// Native helpers for two of the title's C runtime functions on hot paths
// (docs/analysis/career-season-soak.md): strstr, which the first draw of a new car
// calls for its material names, and a memo of pow for SetGammaRamp, which
// recomputes its gamma ramp with pow every frame. Header only, so the CPU tests
// (tests/crt-helpers-tests.cpp) run them without the runtime.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace fm4::crt {

// The title's strstr (sub_82C88DA0) on guest memory at `memory`: the address of
// the first `needle` in `haystack`, the haystack itself for an empty needle, 0
// when there is none. Both strings must lie in one host-contiguous range (the
// caller checks the guest addresses).
inline uint32_t Strstr(const uint8_t* memory, uint32_t haystack, uint32_t needle) {
  const char* found = std::strstr(reinterpret_cast<const char*>(memory + haystack),
                                  reinterpret_cast<const char*>(memory + needle));
  return found ? uint32_t(reinterpret_cast<const uint8_t*>(found) - memory) : 0;
}

// The PowerPC loop of the title's strstr, byte for byte (signed bytes, as its
// extsb), for the tests to compare with.
inline uint32_t StrstrReference(const uint8_t* memory, uint32_t haystack, uint32_t needle) {
  if (!memory[needle]) return haystack;
  for (uint32_t start = haystack; memory[start]; ++start) {
    uint32_t i = 0;
    while (memory[needle + i] && int8_t(memory[start + i]) == int8_t(memory[needle + i])) ++i;
    if (!memory[needle + i]) return start;
  }
  return 0;
}

// Whether a pow result may be kept: a finite positive base, a finite exponent and
// a finite normal result. The title's pow takes its error paths (zero, negative or
// special bases, overflow and underflow, where it calls the C runtime's error
// handling) only outside these.
inline bool PowCacheable(double base, double exponent, double result) {
  return std::isfinite(base) && base > 0 && std::isfinite(exponent) && std::isnormal(result);
}

// A bounded table of (base, exponent) -> result by their bits, open addressing.
// It stops taking entries at three quarters of its slots; a lookup then misses
// and the caller runs the guest pow, so a title that animates its gamma cannot
// grow it or make a miss cost more than a probe of a few slots.
class PowMemo {
 public:
  explicit PowMemo(uint32_t slots_log2 = 16) : slots_(size_t(1) << slots_log2) {}

  bool Find(uint64_t base, uint64_t exponent, uint64_t* result) {
    ++lookups_;
    for (size_t i = Hash(base, exponent), probes = 0; probes < kMaxProbes; ++probes, i = Next(i)) {
      const Slot& slot = slots_[i];
      if (!slot.used) return false;
      if (slot.base == base && slot.exponent == exponent) {
        *result = slot.result;
        ++hits_;
        return true;
      }
    }
    return false;
  }

  void Insert(uint64_t base, uint64_t exponent, uint64_t result) {
    if (entries_ >= slots_.size() * 3 / 4) {
      full_ = true;
      return;
    }
    for (size_t i = Hash(base, exponent), probes = 0; probes < kMaxProbes; ++probes, i = Next(i)) {
      Slot& slot = slots_[i];
      if (slot.used && (slot.base != base || slot.exponent != exponent)) continue;
      if (!slot.used) ++entries_;
      slot = {base, exponent, result, true};
      return;
    }
  }

  uint64_t lookups() const { return lookups_; }
  uint64_t hits() const { return hits_; }
  size_t entries() const { return entries_; }
  size_t capacity() const { return slots_.size() * 3 / 4; }
  bool full() const { return full_; }

 private:
  static constexpr size_t kMaxProbes = 16;
  struct Slot {
    uint64_t base = 0, exponent = 0, result = 0;
    bool used = false;
  };
  size_t Hash(uint64_t base, uint64_t exponent) const {
    uint64_t h = base * 0x9E3779B97F4A7C15ull ^ (exponent + 0x632BE59BD9B4E019ull) * 0xC2B2AE3D27D4EB4Full;
    h ^= h >> 29;
    return size_t(h) & (slots_.size() - 1);
  }
  size_t Next(size_t i) const { return (i + 1) & (slots_.size() - 1); }

  std::vector<Slot> slots_;
  size_t entries_ = 0;
  uint64_t lookups_ = 0, hits_ = 0;
  bool full_ = false;
};

// FNV-1a over a span: the gamma ramp hash of FM4_GAMMA_RAMP_HASH.
inline uint64_t Fnv1a(const uint8_t* data, size_t size) {
  uint64_t hash = 0xCBF29CE484222325ull;
  for (size_t i = 0; i < size; ++i) hash = (hash ^ data[i]) * 0x100000001B3ull;
  return hash;
}

}  // namespace fm4::crt
