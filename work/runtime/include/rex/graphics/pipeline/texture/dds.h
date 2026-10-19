/**
 * @file        graphics/pipeline/texture/dds.h
 * @brief       DDS reading and writing for texture dumps and replacements
 *
 * Header only and free of runtime dependencies (no Direct3D headers), so CPU
 * tests can include it. Formats are DXGI_FORMAT values as plain integers.
 * Only 2D textures with one array slice are supported: no cube maps, volumes
 * or arrays.
 */

#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace rex::graphics::texture_dds {

// The DXGI_FORMAT values this file knows.
enum Format : uint32_t {
  kFormatUnknown = 0,
  kR16G16B16A16Float = 10,
  kR10G10B10A2Unorm = 24,
  kR8G8B8A8Unorm = 28,
  kR8G8B8A8UnormSrgb = 29,
  kR8G8Unorm = 49,
  kR8Unorm = 61,
  kBC1Unorm = 71,
  kBC1UnormSrgb = 72,
  kBC2Unorm = 74,
  kBC2UnormSrgb = 75,
  kBC3Unorm = 77,
  kBC3UnormSrgb = 78,
  kBC4Unorm = 80,
  kBC5Unorm = 83,
  kB5G6R5Unorm = 85,
  kB8G8R8A8Unorm = 87,
  kB8G8R8X8Unorm = 88,
  kB8G8R8A8UnormSrgb = 91,
  kBC6HUF16 = 95,
  kBC7Unorm = 98,
  kBC7UnormSrgb = 99,
};

struct FormatLayout {
  // 4 for block-compressed formats, 1 otherwise.
  uint32_t block_size;
  uint32_t bytes_per_block;
};

// Returns false for formats this file does not handle.
inline bool GetFormatLayout(uint32_t format, FormatLayout& out) {
  switch (format) {
    case kBC1Unorm:
    case kBC1UnormSrgb:
    case kBC4Unorm:
      out = {4, 8};
      return true;
    case kBC2Unorm:
    case kBC2UnormSrgb:
    case kBC3Unorm:
    case kBC3UnormSrgb:
    case kBC5Unorm:
    case kBC6HUF16:
    case kBC7Unorm:
    case kBC7UnormSrgb:
      out = {4, 16};
      return true;
    case kR8Unorm:
      out = {1, 1};
      return true;
    case kR8G8Unorm:
    case kB5G6R5Unorm:
      out = {1, 2};
      return true;
    case kR8G8B8A8Unorm:
    case kR8G8B8A8UnormSrgb:
    case kR10G10B10A2Unorm:
    case kB8G8R8A8Unorm:
    case kB8G8R8X8Unorm:
    case kB8G8R8A8UnormSrgb:
      out = {1, 4};
      return true;
    case kR16G16B16A16Float:
      out = {1, 8};
      return true;
    default:
      return false;
  }
}

// The guest shaders decode gamma themselves, so a replacement is sampled as the
// raw stored values: sRGB formats are read as their UNORM equivalents.
inline uint32_t StripSrgb(uint32_t format) {
  switch (format) {
    case kR8G8B8A8UnormSrgb:
      return kR8G8B8A8Unorm;
    case kBC1UnormSrgb:
      return kBC1Unorm;
    case kBC2UnormSrgb:
      return kBC2Unorm;
    case kBC3UnormSrgb:
      return kBC3Unorm;
    case kB8G8R8A8UnormSrgb:
      return kB8G8R8A8Unorm;
    case kBC7UnormSrgb:
      return kBC7Unorm;
    default:
      return format;
  }
}

struct Level {
  uint32_t width;
  uint32_t height;
  // Offset of the level in Image::data.
  size_t offset;
  // Bytes in one row of blocks (of pixels for uncompressed formats), tightly
  // packed.
  uint32_t row_pitch;
  // Rows of blocks.
  uint32_t row_count;
  size_t size() const { return size_t(row_pitch) * row_count; }
};

struct Image {
  uint32_t format = kFormatUnknown;
  uint32_t width = 0;
  uint32_t height = 0;
  std::vector<Level> levels;
  std::vector<uint8_t> data;
};

// Fills the level layout (tightly packed, levels one after another) for the
// format, size and level count, and returns the total data size, or 0 if the
// format is not supported.
inline size_t LayoutLevels(uint32_t format, uint32_t width, uint32_t height, uint32_t level_count,
                           std::vector<Level>& levels_out) {
  levels_out.clear();
  FormatLayout layout;
  if (!GetFormatLayout(format, layout) || !width || !height || !level_count) {
    return 0;
  }
  size_t offset = 0;
  for (uint32_t i = 0; i < level_count; ++i) {
    Level level;
    level.width = std::max(width >> i, uint32_t(1));
    level.height = std::max(height >> i, uint32_t(1));
    level.offset = offset;
    level.row_pitch =
        (level.width + layout.block_size - 1) / layout.block_size * layout.bytes_per_block;
    level.row_count = (level.height + layout.block_size - 1) / layout.block_size;
    offset += level.size();
    levels_out.push_back(level);
  }
  return offset;
}

namespace detail {

constexpr uint32_t kMagic = 0x20534444;  // "DDS "
constexpr uint32_t kHeaderSize = 124;
constexpr uint32_t kPixelFormatSize = 32;
constexpr uint32_t kDx10HeaderSize = 20;

constexpr uint32_t kFlagCaps = 0x1;
constexpr uint32_t kFlagHeight = 0x2;
constexpr uint32_t kFlagWidth = 0x4;
constexpr uint32_t kFlagPitch = 0x8;
constexpr uint32_t kFlagPixelFormat = 0x1000;
constexpr uint32_t kFlagMipMapCount = 0x20000;
constexpr uint32_t kFlagLinearSize = 0x80000;
constexpr uint32_t kFlagDepth = 0x800000;

constexpr uint32_t kPixelAlphaPixels = 0x1;
constexpr uint32_t kPixelFourCC = 0x4;
constexpr uint32_t kPixelRgb = 0x40;
constexpr uint32_t kPixelLuminance = 0x20000;

constexpr uint32_t kCapsComplex = 0x8;
constexpr uint32_t kCapsTexture = 0x1000;
constexpr uint32_t kCapsMipMap = 0x400000;
constexpr uint32_t kCaps2CubeMap = 0x200;
constexpr uint32_t kCaps2Volume = 0x200000;

constexpr uint32_t kDx10Texture2D = 3;
constexpr uint32_t kDx10MiscTextureCube = 0x4;

constexpr uint32_t FourCC(char a, char b, char c, char d) {
  return uint32_t(uint8_t(a)) | (uint32_t(uint8_t(b)) << 8) | (uint32_t(uint8_t(c)) << 16) |
         (uint32_t(uint8_t(d)) << 24);
}

inline uint32_t Read32(const uint8_t* p) {
  uint32_t v;
  std::memcpy(&v, p, sizeof(v));
  return v;
}

inline void Write32(std::vector<uint8_t>& out, size_t offset, uint32_t v) {
  std::memcpy(out.data() + offset, &v, sizeof(v));
}

// Legacy (pre-DX10) pixel format to DXGI, or 0.
inline uint32_t LegacyFormat(const uint8_t* pf) {
  uint32_t flags = Read32(pf + 4);
  uint32_t fourcc = Read32(pf + 8);
  uint32_t bits = Read32(pf + 12);
  uint32_t r = Read32(pf + 16), g = Read32(pf + 20), b = Read32(pf + 24), a = Read32(pf + 28);
  if (flags & kPixelFourCC) {
    if (fourcc == FourCC('D', 'X', 'T', '1')) return kBC1Unorm;
    if (fourcc == FourCC('D', 'X', 'T', '2') || fourcc == FourCC('D', 'X', 'T', '3')) {
      return kBC2Unorm;
    }
    if (fourcc == FourCC('D', 'X', 'T', '4') || fourcc == FourCC('D', 'X', 'T', '5')) {
      return kBC3Unorm;
    }
    if (fourcc == FourCC('A', 'T', 'I', '1') || fourcc == FourCC('B', 'C', '4', 'U')) {
      return kBC4Unorm;
    }
    if (fourcc == FourCC('A', 'T', 'I', '2') || fourcc == FourCC('B', 'C', '5', 'U')) {
      return kBC5Unorm;
    }
    // D3DFMT_A16B16G16R16F.
    if (fourcc == 113) return kR16G16B16A16Float;
    return kFormatUnknown;
  }
  if ((flags & kPixelRgb) && bits == 32) {
    if (r == 0xFF && g == 0xFF00 && b == 0xFF0000) {
      return (flags & kPixelAlphaPixels) || a == 0xFF000000 ? kR8G8B8A8Unorm : kFormatUnknown;
    }
    if (r == 0xFF0000 && g == 0xFF00 && b == 0xFF) {
      return a == 0xFF000000 ? kB8G8R8A8Unorm : kB8G8R8X8Unorm;
    }
    if (r == 0x3FF && g == 0xFFC00 && b == 0x3FF00000) return kR10G10B10A2Unorm;
    return kFormatUnknown;
  }
  if ((flags & kPixelRgb) && bits == 16 && r == 0xF800 && g == 0x7E0 && b == 0x1F) {
    return kB5G6R5Unorm;
  }
  if ((flags & (kPixelLuminance | kPixelRgb)) && bits == 8 && r == 0xFF) {
    return kR8Unorm;
  }
  return kFormatUnknown;
}

}  // namespace detail

// Parses a DDS file. On failure returns false and sets the error.
inline bool Parse(const uint8_t* file, size_t file_size, Image& out, std::string& error) {
  using namespace detail;
  out = Image();
  if (file_size < 4 + kHeaderSize || Read32(file) != kMagic) {
    error = "not a DDS file";
    return false;
  }
  const uint8_t* header = file + 4;
  if (Read32(header) != kHeaderSize || Read32(header + 72) != kPixelFormatSize) {
    error = "bad DDS header size";
    return false;
  }
  uint32_t flags = Read32(header + 4);
  uint32_t height = Read32(header + 8);
  uint32_t width = Read32(header + 12);
  uint32_t depth = Read32(header + 20);
  uint32_t mip_count = Read32(header + 24);
  const uint8_t* pixel_format = header + 72;
  uint32_t caps2 = Read32(header + 108);
  if ((caps2 & (kCaps2CubeMap | kCaps2Volume)) || ((flags & kFlagDepth) && depth > 1)) {
    error = "cube maps and volume textures are not supported";
    return false;
  }
  size_t data_offset = 4 + kHeaderSize;
  uint32_t format;
  if ((Read32(pixel_format + 4) & kPixelFourCC) &&
      Read32(pixel_format + 8) == FourCC('D', 'X', '1', '0')) {
    if (file_size < data_offset + kDx10HeaderSize) {
      error = "truncated DX10 header";
      return false;
    }
    const uint8_t* dx10 = file + data_offset;
    format = Read32(dx10);
    if (Read32(dx10 + 4) != kDx10Texture2D || (Read32(dx10 + 8) & kDx10MiscTextureCube) ||
        Read32(dx10 + 12) > 1) {
      error = "only 2D textures with one array slice are supported";
      return false;
    }
    data_offset += kDx10HeaderSize;
  } else {
    format = LegacyFormat(pixel_format);
  }
  FormatLayout layout;
  if (!GetFormatLayout(format, layout)) {
    error = "unsupported pixel format (DXGI " + std::to_string(format) + ")";
    return false;
  }
  if (!width || !height || width > 16384 || height > 16384) {
    error = "bad size";
    return false;
  }
  if (!(flags & kFlagMipMapCount) || mip_count == 0) {
    mip_count = 1;
  }
  uint32_t max_mips = 1;
  while ((std::max(width, height) >> max_mips) != 0) {
    ++max_mips;
  }
  mip_count = std::min(mip_count, max_mips);
  size_t data_size = LayoutLevels(format, width, height, mip_count, out.levels);
  if (file_size - data_offset < data_size) {
    error = "truncated pixel data";
    out.levels.clear();
    return false;
  }
  out.format = format;
  out.width = width;
  out.height = height;
  out.data.assign(file + data_offset, file + data_offset + data_size);
  return true;
}

// Writes a DDS file: a legacy header where one exists for the format (BC1-BC5,
// RGBA8, BGRA8, R8, B5G6R5), so older tools open it, a DX10 header otherwise.
// The image's levels must be tightly packed as LayoutLevels lays them out.
inline std::vector<uint8_t> Write(const Image& image) {
  using namespace detail;
  uint32_t legacy_fourcc = 0, legacy_bits = 0, legacy_flags = 0;
  uint32_t masks[4] = {};
  switch (image.format) {
    case kBC1Unorm:
      legacy_fourcc = FourCC('D', 'X', 'T', '1');
      break;
    case kBC2Unorm:
      legacy_fourcc = FourCC('D', 'X', 'T', '3');
      break;
    case kBC3Unorm:
      legacy_fourcc = FourCC('D', 'X', 'T', '5');
      break;
    case kBC4Unorm:
      legacy_fourcc = FourCC('A', 'T', 'I', '1');
      break;
    case kBC5Unorm:
      legacy_fourcc = FourCC('A', 'T', 'I', '2');
      break;
    case kR8G8B8A8Unorm:
      legacy_flags = kPixelRgb | kPixelAlphaPixels;
      legacy_bits = 32;
      masks[0] = 0xFF, masks[1] = 0xFF00, masks[2] = 0xFF0000, masks[3] = 0xFF000000;
      break;
    case kB8G8R8A8Unorm:
      legacy_flags = kPixelRgb | kPixelAlphaPixels;
      legacy_bits = 32;
      masks[0] = 0xFF0000, masks[1] = 0xFF00, masks[2] = 0xFF, masks[3] = 0xFF000000;
      break;
    case kB5G6R5Unorm:
      legacy_flags = kPixelRgb;
      legacy_bits = 16;
      masks[0] = 0xF800, masks[1] = 0x7E0, masks[2] = 0x1F;
      break;
    case kR8Unorm:
      legacy_flags = kPixelLuminance;
      legacy_bits = 8;
      masks[0] = 0xFF;
      break;
    default:
      break;
  }
  bool dx10 = !legacy_fourcc && !legacy_flags;
  FormatLayout layout;
  bool compressed = GetFormatLayout(image.format, layout) && layout.block_size > 1;
  uint32_t mip_count = uint32_t(image.levels.size());
  size_t header_end = 4 + kHeaderSize + (dx10 ? kDx10HeaderSize : 0);
  std::vector<uint8_t> out(header_end + image.data.size(), 0);
  Write32(out, 0, kMagic);
  size_t h = 4;
  Write32(out, h + 0, kHeaderSize);
  Write32(out, h + 4,
          kFlagCaps | kFlagHeight | kFlagWidth | kFlagPixelFormat |
              (mip_count > 1 ? kFlagMipMapCount : 0) | (compressed ? kFlagLinearSize : kFlagPitch));
  Write32(out, h + 8, image.height);
  Write32(out, h + 12, image.width);
  Write32(out, h + 16,
          image.levels.empty()
              ? 0
              : uint32_t(compressed ? image.levels[0].size() : image.levels[0].row_pitch));
  Write32(out, h + 24, mip_count);
  size_t pf = h + 72;
  Write32(out, pf + 0, kPixelFormatSize);
  if (dx10) {
    Write32(out, pf + 4, kPixelFourCC);
    Write32(out, pf + 8, FourCC('D', 'X', '1', '0'));
  } else if (legacy_fourcc) {
    Write32(out, pf + 4, kPixelFourCC);
    Write32(out, pf + 8, legacy_fourcc);
  } else {
    Write32(out, pf + 4, legacy_flags);
    Write32(out, pf + 12, legacy_bits);
    for (int i = 0; i < 4; ++i) Write32(out, pf + 16 + 4 * i, masks[i]);
  }
  Write32(out, h + 104, kCapsTexture | (mip_count > 1 ? kCapsComplex | kCapsMipMap : 0));
  if (dx10) {
    size_t d = 4 + kHeaderSize;
    Write32(out, d + 0, image.format);
    Write32(out, d + 4, kDx10Texture2D);
    Write32(out, d + 8, 0);
    Write32(out, d + 12, 1);
    Write32(out, d + 16, 0);
  }
  if (!image.data.empty()) {
    std::memcpy(out.data() + header_end, image.data.data(), image.data.size());
  }
  return out;
}

}  // namespace rex::graphics::texture_dds
