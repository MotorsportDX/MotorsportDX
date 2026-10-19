// CPU tests of texture replacement (docs/analysis/texture-replacement.md):
// the content hash (rex/graphics/pipeline/texture/content_hash.h) stays the
// same across runs and releases, and DDS files (rex/graphics/pipeline/texture/
// dds.h) round trip for BC1, BC3, BC5, BC7, RGBA8 and the other formats the
// dumps write, with legacy and DX10 headers. Synthetic data only; returns 1 on
// failure.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <rex/graphics/pipeline/texture/content_hash.h>
#include <rex/graphics/pipeline/texture/dds.h>

namespace {

namespace dds = rex::graphics::texture_dds;
namespace content_hash = rex::graphics::texture_content_hash;

int failures = 0;

void Check(bool condition, const std::string& what) {
  if (!condition) {
    std::printf("FAIL %s\n", what.c_str());
    ++failures;
  }
}

// Deterministic bytes.
std::vector<uint8_t> Pattern(size_t size, uint32_t seed) {
  std::vector<uint8_t> out(size);
  uint32_t x = seed * 2654435761u + 1;
  for (size_t i = 0; i < size; ++i) {
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    out[i] = uint8_t(x);
  }
  return out;
}

// A 64x32 DXT1 (xenos::TextureFormat::k_DXT1 = 18) image: 16x8 blocks of 8
// bytes.
constexpr uint32_t kDxt1 = 18;
constexpr uint32_t kRowPitch = 16 * 8;
constexpr uint32_t kRows = 8;

uint64_t HashRows(uint32_t format, uint32_t width, uint32_t height, const std::vector<uint8_t>& rows,
                  uint32_t row_pitch, uint32_t row_size, uint32_t row_count) {
  content_hash::Hasher hasher(format, width, height);
  for (uint32_t y = 0; y < row_count; ++y) {
    hasher.AddRow(rows.data() + size_t(y) * row_pitch, row_size);
  }
  return hasher.Finish();
}

void TestHash() {
  std::vector<uint8_t> blocks = Pattern(kRowPitch * kRows, 1);
  uint64_t hash = HashRows(kDxt1, 64, 32, blocks, kRowPitch, kRowPitch, kRows);

  // Fixed for kVersion 1: XXH3 64 is a frozen format, so this value must not
  // change between runs, builds or releases. Changing it breaks every texture
  // pack.
  constexpr uint64_t kExpected = 0x0d2101ca995d4d48ull;
  std::printf("hash of the 64x32 DXT1 pattern: %s\n", content_hash::HashName(hash).c_str());
  Check(hash == kExpected, "hash of the 64x32 DXT1 pattern is " + content_hash::HashName(hash) +
                               ", expected " + content_hash::HashName(kExpected));

  // The same texels from a padded guest pitch (the padding is garbage): the
  // hash only sees the bytes inside the width.
  constexpr uint32_t kPaddedPitch = 32 * 8;
  std::vector<uint8_t> padded = Pattern(kPaddedPitch * kRows, 2);
  for (uint32_t y = 0; y < kRows; ++y) {
    std::memcpy(padded.data() + y * kPaddedPitch, blocks.data() + y * kRowPitch, kRowPitch);
  }
  Check(HashRows(kDxt1, 64, 32, padded, kPaddedPitch, kRowPitch, kRows) == hash,
        "hash depends on the guest pitch or its padding");

  // Rows fed at once or one by one: the streaming hash does not depend on the
  // split.
  content_hash::Hasher whole(kDxt1, 64, 32);
  whole.AddRow(blocks.data(), blocks.size());
  Check(whole.Finish() == hash, "hash depends on how the rows are split");

  // Format, size and content all change it.
  Check(HashRows(kDxt1 + 2, 64, 32, blocks, kRowPitch, kRowPitch, kRows) != hash,
        "hash ignores the format");
  Check(HashRows(kDxt1, 64, 31, blocks, kRowPitch, kRowPitch, kRows) != hash,
        "hash ignores the height");
  Check(HashRows(kDxt1, 63, 32, blocks, kRowPitch, kRowPitch, kRows) != hash,
        "hash ignores the width");
  std::vector<uint8_t> changed = blocks;
  changed[kRowPitch * 5 + 3] ^= 1;
  Check(HashRows(kDxt1, 64, 32, changed, kRowPitch, kRowPitch, kRows) != hash,
        "hash ignores one changed bit");

  // File names.
  uint64_t parsed = 0;
  Check(content_hash::HashName(0x0123456789abcdefull) == "0123456789abcdef", "HashName");
  Check(content_hash::ParseHashName("0123456789ABCDEF", parsed) && parsed == 0x0123456789abcdefull,
        "ParseHashName of upper case");
  Check(!content_hash::ParseHashName("0123456789abcde", parsed), "ParseHashName of 15 digits");
  Check(!content_hash::ParseHashName("0123456789abcdeg", parsed), "ParseHashName of a non-digit");
}

uint32_t Read32(const std::vector<uint8_t>& file, size_t offset) {
  uint32_t v;
  std::memcpy(&v, file.data() + offset, 4);
  return v;
}

constexpr uint32_t FourCC(const char* s) {
  return uint32_t(uint8_t(s[0])) | (uint32_t(uint8_t(s[1])) << 8) |
         (uint32_t(uint8_t(s[2])) << 16) | (uint32_t(uint8_t(s[3])) << 24);
}

struct RoundTrip {
  const char* name;
  uint32_t format;
  uint32_t width, height, levels;
  // FourCC of the legacy header, "DX10", or nullptr for a legacy RGB header.
  const char* fourcc;
  // Expected size of level 0.
  size_t level0_size;
};

void TestRoundTrip(const RoundTrip& t) {
  std::string name = t.name;
  dds::Image image;
  image.format = t.format;
  image.width = t.width;
  image.height = t.height;
  size_t size = dds::LayoutLevels(t.format, t.width, t.height, t.levels, image.levels);
  Check(size != 0, name + ": LayoutLevels");
  if (!size) return;
  Check(image.levels[0].size() == t.level0_size, name + ": level 0 size");
  Check(image.levels.back().width == std::max(t.width >> (t.levels - 1), 1u) &&
            image.levels.back().height == std::max(t.height >> (t.levels - 1), 1u),
        name + ": last level size");
  image.data = Pattern(size, t.format);
  std::vector<uint8_t> file = dds::Write(image);

  Check(Read32(file, 0) == FourCC("DDS "), name + ": magic");
  uint32_t pf_flags = Read32(file, 80), fourcc = Read32(file, 84);
  if (t.fourcc) {
    Check((pf_flags & 0x4) && fourcc == FourCC(t.fourcc), name + ": header FourCC");
  } else {
    Check(!(pf_flags & 0x4), name + ": legacy RGB header");
  }

  dds::Image parsed;
  std::string error;
  bool ok = dds::Parse(file.data(), file.size(), parsed, error);
  Check(ok, name + ": Parse: " + error);
  if (!ok) return;
  Check(parsed.format == t.format, name + ": format " + std::to_string(parsed.format));
  Check(parsed.width == t.width && parsed.height == t.height, name + ": size");
  Check(parsed.levels.size() == t.levels, name + ": level count");
  Check(parsed.data == image.data, name + ": data");
  for (size_t i = 0; i < parsed.levels.size() && i < image.levels.size(); ++i) {
    Check(parsed.levels[i].offset == image.levels[i].offset &&
              parsed.levels[i].row_pitch == image.levels[i].row_pitch &&
              parsed.levels[i].row_count == image.levels[i].row_count,
          name + ": level " + std::to_string(i) + " layout");
  }

  // One byte short: rejected, not read past the end.
  file.pop_back();
  Check(!dds::Parse(file.data(), file.size(), parsed, error), name + ": truncated file accepted");
}

void TestDds() {
  const RoundTrip cases[] = {
      // 256x128 BC1: 64x32 blocks of 8 bytes.
      {"BC1", dds::kBC1Unorm, 256, 128, 9, "DXT1", 64 * 32 * 8},
      {"BC3", dds::kBC3Unorm, 256, 256, 9, "DXT5", 64 * 64 * 16},
      {"BC5", dds::kBC5Unorm, 128, 64, 8, "ATI2", 32 * 16 * 16},
      {"BC7", dds::kBC7Unorm, 512, 256, 10, "DX10", 128 * 64 * 16},
      {"RGBA8", dds::kR8G8B8A8Unorm, 64, 32, 7, nullptr, 64 * 32 * 4},
      {"BGRA8", dds::kB8G8R8A8Unorm, 16, 16, 1, nullptr, 16 * 16 * 4},
      {"R8", dds::kR8Unorm, 32, 8, 6, nullptr, 32 * 8},
      {"BC2", dds::kBC2Unorm, 64, 64, 1, "DXT3", 16 * 16 * 16},
      {"BC4", dds::kBC4Unorm, 64, 64, 7, "ATI1", 16 * 16 * 8},
      // Not a multiple of 4: blocks round up (the guest has such textures).
      {"BC1 5x3", dds::kBC1Unorm, 5, 3, 3, "DXT1", 2 * 1 * 8},
      {"RG8", dds::kR8G8Unorm, 8, 8, 4, "DX10", 8 * 8 * 2},
      {"RGBA16F", dds::kR16G16B16A16Float, 8, 4, 1, "DX10", 8 * 4 * 8},
  };
  for (const RoundTrip& t : cases) {
    TestRoundTrip(t);
  }

  std::string error;
  dds::Image parsed;

  // A DX10 BC7 sRGB file as texconv writes it: parsed, and read as UNORM.
  {
    dds::Image image;
    image.format = dds::kBC7UnormSrgb;
    image.width = image.height = 8;
    image.data = Pattern(dds::LayoutLevels(image.format, 8, 8, 1, image.levels), 7);
    std::vector<uint8_t> file = dds::Write(image);
    Check(dds::Parse(file.data(), file.size(), parsed, error) &&
              parsed.format == dds::kBC7UnormSrgb,
          "BC7 sRGB: " + error);
    Check(dds::StripSrgb(parsed.format) == dds::kBC7Unorm, "BC7 sRGB read as UNORM");
  }

  // Legacy RGBA8 without the mip count flag: one level, even with a count.
  {
    dds::Image image;
    image.format = dds::kR8G8B8A8Unorm;
    image.width = image.height = 4;
    image.data = Pattern(dds::LayoutLevels(image.format, 4, 4, 1, image.levels), 8);
    std::vector<uint8_t> file = dds::Write(image);
    uint32_t mips = 3;
    std::memcpy(file.data() + 4 + 24, &mips, 4);
    Check(dds::Parse(file.data(), file.size(), parsed, error) && parsed.levels.size() == 1,
          "mip count without its flag");
    // More levels than the size allows are clamped.
    uint32_t flags = Read32(file, 8) | 0x20000;
    std::memcpy(file.data() + 8, &flags, 4);
    mips = 12;
    std::memcpy(file.data() + 4 + 24, &mips, 4);
    file.resize(file.size() + 64, 0);
    Check(dds::Parse(file.data(), file.size(), parsed, error) && parsed.levels.size() == 3,
          "mip count clamped to the size: " + std::to_string(parsed.levels.size()));
  }

  // Rejected: not DDS, a cube map, an unknown format.
  {
    dds::Image image;
    image.format = dds::kBC1Unorm;
    image.width = image.height = 4;
    image.data = Pattern(dds::LayoutLevels(image.format, 4, 4, 1, image.levels), 9);
    std::vector<uint8_t> file = dds::Write(image);
    std::vector<uint8_t> bad = file;
    bad[0] = 'X';
    Check(!dds::Parse(bad.data(), bad.size(), parsed, error), "bad magic accepted");
    bad = file;
    uint32_t caps2 = 0x200 | 0xFC00;
    std::memcpy(bad.data() + 4 + 108, &caps2, 4);
    Check(!dds::Parse(bad.data(), bad.size(), parsed, error), "cube map accepted");
    bad = file;
    uint32_t fourcc = FourCC("ABCD");
    std::memcpy(bad.data() + 84, &fourcc, 4);
    Check(!dds::Parse(bad.data(), bad.size(), parsed, error), "unknown FourCC accepted");
    Check(!dds::Parse(file.data(), 64, parsed, error), "short header accepted");
  }
}

}  // namespace

int main() {
  TestHash();
  TestDds();
  if (failures) {
    std::printf("%d failures\n", failures);
    return 1;
  }
  std::printf("texture replacement tests passed\n");
  return 0;
}
