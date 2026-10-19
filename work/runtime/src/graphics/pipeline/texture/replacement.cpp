/**
 * @file        graphics/pipeline/texture/replacement.cpp
 * @brief       Texture dumps and replacements keyed by content hash (CPU side)
 */

#include <rex/graphics/pipeline/texture/replacement.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iterator>
#include <system_error>

#include <rex/graphics/pipeline/texture/content_hash.h>
#include <rex/graphics/pipeline/texture/info.h>
#include <rex/logging.h>

namespace rex::graphics::texture_replacement {

namespace {

uint32_t Log2(uint32_t value) {
  uint32_t log2 = 0;
  while ((UINT32_C(1) << (log2 + 1)) <= value) {
    ++log2;
  }
  return log2;
}

bool IsReplacementFile(const std::filesystem::path& path, uint64_t& hash_out) {
  std::string extension = path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char c) { return char(std::tolower(c)); });
  return extension == ".dds" && texture_content_hash::ParseHashName(path.stem().string(), hash_out);
}

}  // namespace

bool ReadGuestBaseLevel(const uint8_t* guest_base, xenos::TextureFormat format, uint32_t width,
                        uint32_t height, bool tiled, xenos::Endian endianness,
                        const texture_util::TextureGuestLayout& layout, GuestLevel& out) {
  const FormatInfo* format_info = FormatInfo::Get(format);
  uint32_t bytes_per_block = format_info->bytes_per_block();
  if (!guest_base || !bytes_per_block || bytes_per_block > 16 ||
      (bytes_per_block & (bytes_per_block - 1))) {
    return false;
  }
  const texture_util::TextureGuestLayout::Level& level = layout.base;
  uint32_t extent = level.level_data_extent_bytes;
  if (!extent || !level.row_pitch_bytes) {
    return false;
  }
  // The base level is in the packed mip tail of small textures, at an offset.
  uint32_t offset_x = 0, offset_y = 0;
  if (layout.packed_level == 0) {
    uint32_t offset_z;
    texture_util::GetPackedMipOffset(width, height, 1, format, 0, offset_x, offset_y, offset_z);
  }

  // Swap the whole extent as the GPU fetches it, in 32-bit words, before
  // untiling: blocks smaller than 4 bytes share their words with neighbors.
  std::vector<uint8_t> swapped((extent + 3) & ~uint32_t(3), 0);
  std::memcpy(swapped.data(), guest_base, extent);
  if (endianness != xenos::Endian::kNone) {
    for (size_t i = 0; i < swapped.size(); i += 4) {
      uint32_t word;
      std::memcpy(&word, swapped.data() + i, 4);
      word = xenos::GpuSwap(word, endianness);
      std::memcpy(swapped.data() + i, &word, 4);
    }
  }

  uint32_t blocks_x = (width + format_info->block_width - 1) / format_info->block_width;
  uint32_t blocks_y = (height + format_info->block_height - 1) / format_info->block_height;
  out.row_pitch = blocks_x * bytes_per_block;
  out.row_count = blocks_y;
  out.blocks.resize(size_t(out.row_pitch) * blocks_y);
  uint32_t bytes_per_block_log2 = Log2(bytes_per_block);
  uint32_t pitch_blocks = level.row_pitch_bytes / bytes_per_block;
  for (uint32_t y = 0; y < blocks_y; ++y) {
    uint8_t* row = out.blocks.data() + size_t(y) * out.row_pitch;
    if (!tiled) {
      size_t source = size_t(y + offset_y) * level.row_pitch_bytes + size_t(offset_x) * bytes_per_block;
      if (source + out.row_pitch > swapped.size()) {
        return false;
      }
      std::memcpy(row, swapped.data() + source, out.row_pitch);
      continue;
    }
    for (uint32_t x = 0; x < blocks_x; ++x) {
      int32_t source = texture_util::GetTiledOffset2D(int32_t(x + offset_x), int32_t(y + offset_y),
                                                      pitch_blocks, bytes_per_block_log2);
      if (source < 0 || size_t(source) + bytes_per_block > swapped.size()) {
        return false;
      }
      std::memcpy(row + size_t(x) * bytes_per_block, swapped.data() + source, bytes_per_block);
    }
  }
  return true;
}

uint64_t HashGuestLevel(xenos::TextureFormat format, uint32_t width, uint32_t height,
                        const GuestLevel& level) {
  texture_content_hash::Hasher hasher(uint32_t(format), width, height);
  for (uint32_t y = 0; y < level.row_count; ++y) {
    hasher.AddRow(level.blocks.data() + size_t(y) * level.row_pitch, level.row_pitch);
  }
  return hasher.Finish();
}

size_t Store::Initialize(const std::filesystem::path& directory) {
  enabled_ = false;
  files_.clear();
  images_.clear();
  failed_.clear();
  std::error_code ec;
  if (directory.empty() || !std::filesystem::is_directory(directory, ec)) {
    if (!directory.empty()) {
      REXGPU_WARN("texture_replace_path {} is not a directory", directory.string());
    }
    return 0;
  }
  for (std::filesystem::recursive_directory_iterator it(directory, ec), end; !ec && it != end;
       it.increment(ec)) {
    uint64_t hash;
    if (it->is_regular_file(ec) && IsReplacementFile(it->path(), hash)) {
      auto [existing, inserted] = files_.emplace(hash, it->path());
      if (!inserted) {
        REXGPU_WARN("Texture replacement {} found twice, using {}",
                    texture_content_hash::HashName(hash), existing->second.string());
      }
    }
  }
  enabled_ = true;
  REXGPU_INFO("Texture replacement: {} files in {}", files_.size(), directory.string());
  return files_.size();
}

const texture_dds::Image* Store::Get(uint64_t hash) {
  if (!Has(hash) || failed_.count(hash)) {
    return nullptr;
  }
  auto cached = images_.find(hash);
  if (cached != images_.end()) {
    return cached->second.get();
  }
  const std::filesystem::path& path = files_[hash];
  std::ifstream file(path, std::ios::binary);
  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());
  auto image = std::make_unique<texture_dds::Image>();
  std::string error = "could not read the file";
  if (!file.bad() && !bytes.empty() &&
      texture_dds::Parse(bytes.data(), bytes.size(), *image, error)) {
    image->format = texture_dds::StripSrgb(image->format);
    REXGPU_INFO("Texture replacement {}: {}x{}, DXGI format {}, {} levels", path.string(),
                image->width, image->height, image->format, image->levels.size());
    return images_.emplace(hash, std::move(image)).first->second.get();
  }
  REXGPU_WARN("Texture replacement {} not used: {}", path.string(), error);
  failed_.insert(hash);
  return nullptr;
}

bool Dumper::Initialize(const std::filesystem::path& directory) {
  enabled_ = false;
  dumped_.clear();
  if (directory.empty()) {
    return false;
  }
  std::error_code ec;
  std::filesystem::create_directories(directory, ec);
  if (!std::filesystem::is_directory(directory, ec)) {
    REXGPU_WARN("texture_dump_path {} could not be created", directory.string());
    return false;
  }
  directory_ = directory;
  // Files from earlier runs are not written again.
  for (std::filesystem::directory_iterator it(directory, ec), end; !ec && it != end;
       it.increment(ec)) {
    uint64_t hash;
    if (IsReplacementFile(it->path(), hash)) {
      dumped_.insert(hash);
    }
  }
  enabled_ = true;
  REXGPU_INFO("Texture dump: {} ({} textures already there)", directory.string(), dumped_.size());
  return true;
}

uint32_t Dumper::GetDumpFormat(xenos::TextureFormat format) {
  using xenos::TextureFormat;
  switch (format) {
    case TextureFormat::k_8:
    case TextureFormat::k_8_A:
      return texture_dds::kR8Unorm;
    case TextureFormat::k_8_8:
      return texture_dds::kR8G8Unorm;
    case TextureFormat::k_8_8_8_8:
    case TextureFormat::k_8_8_8_8_AS_16_16_16_16:
      return texture_dds::kR8G8B8A8Unorm;
    case TextureFormat::k_2_10_10_10:
    case TextureFormat::k_2_10_10_10_AS_16_16_16_16:
      return texture_dds::kR10G10B10A2Unorm;
    case TextureFormat::k_DXT1:
    case TextureFormat::k_DXT1_AS_16_16_16_16:
      return texture_dds::kBC1Unorm;
    case TextureFormat::k_DXT2_3:
    case TextureFormat::k_DXT2_3_AS_16_16_16_16:
      return texture_dds::kBC2Unorm;
    case TextureFormat::k_DXT4_5:
    case TextureFormat::k_DXT4_5_AS_16_16_16_16:
      return texture_dds::kBC3Unorm;
    case TextureFormat::k_DXN:
      return texture_dds::kBC5Unorm;
    case TextureFormat::k_DXT5A:
      return texture_dds::kBC4Unorm;
    case TextureFormat::k_16_16_16_16_FLOAT:
      return texture_dds::kR16G16B16A16Float;
    default:
      return texture_dds::kFormatUnknown;
  }
}

void Dumper::Dump(uint64_t hash, xenos::TextureFormat format, uint32_t width, uint32_t height,
                  uint32_t mip_count, uint32_t guest_address, const GuestLevel& level) {
  if (!enabled_ || !dumped_.insert(hash).second) {
    return;
  }
  std::string name = texture_content_hash::HashName(hash);
  const char* format_name = FormatInfo::Get(format)->name;
  uint32_t dds_format = GetDumpFormat(format);
  bool written = false;
  if (dds_format != texture_dds::kFormatUnknown) {
    texture_dds::Image image;
    image.format = dds_format;
    image.width = width;
    image.height = height;
    size_t size = texture_dds::LayoutLevels(dds_format, width, height, 1, image.levels);
    if (size == level.blocks.size()) {
      image.data = level.blocks;
      std::vector<uint8_t> file = texture_dds::Write(image);
      std::ofstream out(directory_ / (name + ".dds"), std::ios::binary);
      out.write(reinterpret_cast<const char*>(file.data()), std::streamsize(file.size()));
      written = bool(out);
    }
  }
  std::ofstream index(directory_ / "textures.txt", std::ios::app);
  index << name << ' ' << format_name << ' ' << width << 'x' << height << " mips "
        << mip_count << " address 0x" << std::hex << guest_address << std::dec
        << (written ? "" : dds_format ? " write-failed" : " not-dumped-format") << '\n';
}

}  // namespace rex::graphics::texture_replacement
