// CPU tests of the native C runtime helpers (src/fm4-crt-helpers.h): strstr against
// the PowerPC loop it replaces on fixed and random strings, the rule for which pow
// results the memo keeps, and the memo's table (hits, misses, its cap). Returns 1
// on failure.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <random>
#include <string>
#include <vector>

#include "fm4-crt-helpers.h"

namespace {

int failures = 0;

void Check(bool condition, const char* what) {
  if (!condition) {
    std::printf("FAIL %s\n", what);
    ++failures;
  }
}

// Guest memory with two strings at fixed addresses.
struct Memory {
  std::vector<uint8_t> bytes = std::vector<uint8_t>(4096, 0xCC);
  static constexpr uint32_t kHaystack = 0x100, kNeedle = 0x800;
  Memory(const std::string& haystack, const std::string& needle) {
    std::memcpy(bytes.data() + kHaystack, haystack.c_str(), haystack.size() + 1);
    std::memcpy(bytes.data() + kNeedle, needle.c_str(), needle.size() + 1);
  }
  uint32_t Native() const { return fm4::crt::Strstr(bytes.data(), kHaystack, kNeedle); }
  uint32_t Reference() const { return fm4::crt::StrstrReference(bytes.data(), kHaystack, kNeedle); }
};

void Strstr(const std::string& haystack, const std::string& needle, int64_t expected_offset,
            const char* what) {
  Memory memory(haystack, needle);
  const uint32_t expected = expected_offset < 0 ? 0 : Memory::kHaystack + uint32_t(expected_offset);
  Check(memory.Native() == expected, what);
  Check(memory.Reference() == expected, what);
}

}  // namespace

int main() {
  // strstr.
  Strstr("glassLHL_detail", "", 0, "empty needle: the haystack");
  Strstr("", "", 0, "empty haystack and needle");
  Strstr("", "glass", -1, "empty haystack");
  Strstr("detail_glass_red", "chrome", -1, "no match");
  Strstr("detail_glass_red", "detail", 0, "match at the start");
  Strstr("detail_glass_red", "red", 13, "match at the end");
  Strstr("detail_glass_re", "red", -1, "needle past the end");
  Strstr("aaab", "aab", 1, "overlap: the second try matches");
  Strstr("abababc", "ababc", 2, "overlap of a repeated prefix");
  Strstr("glass", "glassLHL", -1, "needle longer than the haystack");
  Strstr("\xC3\xA9t\xC3\xA9", "\xA9t", 1, "bytes above 127");
  std::mt19937 random(7);
  for (int i = 0; i < 200000; ++i) {
    std::string haystack, needle;
    const int haystack_length = int(random() % 24), needle_length = int(random() % 5);
    for (int j = 0; j < haystack_length; ++j) haystack += char('a' + random() % 3);
    for (int j = 0; j < needle_length; ++j) needle += char('a' + random() % 3);
    Memory memory(haystack, needle);
    if (memory.Native() != memory.Reference()) {
      std::printf("FAIL random strstr \"%s\" \"%s\"\n", haystack.c_str(), needle.c_str());
      ++failures;
      break;
    }
  }

  // Which pow results the memo keeps.
  const double inf = std::numeric_limits<double>::infinity(), nan = std::nan("");
  Check(fm4::crt::PowCacheable(0.5, 2.2, std::pow(0.5, 2.2)), "a ramp entry");
  Check(!fm4::crt::PowCacheable(0.0, 2.2, 0.0), "a zero base");
  Check(!fm4::crt::PowCacheable(-0.5, 2.0, 0.25), "a negative base");
  Check(!fm4::crt::PowCacheable(inf, 2.2, inf), "an infinite base");
  Check(!fm4::crt::PowCacheable(0.5, nan, nan), "a NaN exponent");
  Check(!fm4::crt::PowCacheable(1e-300, 2.0, 0.0), "an underflow");
  Check(!fm4::crt::PowCacheable(1e-160, 2.0, 1e-320), "a denormal result");

  // The memo: hits by bits, misses, the cap.
  fm4::crt::PowMemo memo(4);  // 16 slots, 12 entries
  uint64_t result = 0;
  Check(!memo.Find(1, 2, &result), "an empty memo misses");
  memo.Insert(1, 2, 42);
  Check(memo.Find(1, 2, &result) && result == 42, "a hit returns the stored bits");
  Check(!memo.Find(2, 1, &result), "base and exponent are not interchangeable");
  memo.Insert(1, 2, 43);
  Check(memo.Find(1, 2, &result) && result == 43 && memo.entries() == 1, "the same key replaces");
  for (uint64_t i = 0; i < 40; ++i) memo.Insert(100 + i, 7, i);
  Check(memo.entries() == memo.capacity() && memo.full(), "the memo stops at its cap");
  Check(memo.Find(1, 2, &result) && result == 43, "entries before the cap stay");
  Check(!memo.Find(100 + 39, 7, &result), "an insert past the cap is not kept");
  Check(memo.lookups() == 6 && memo.hits() == 3, "the counters");

  if (failures) return 1;
  std::printf("crt helpers tests passed\n");
  return 0;
}
