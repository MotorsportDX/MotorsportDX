/**
 * @file        graphics/pipeline/texture/replacement.h
 * @brief       Texture dumps and replacements keyed by content hash (CPU side)
 *
 * See docs/analysis/texture-replacement.md. The host texture caches compute
 * the content hash of a guest texture when they load it, write it to the dump
 * directory once, and sample <hash>.dds from the replacement directory instead
 * when that file exists.
 */

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <rex/graphics/pipeline/texture/dds.h>
#include <rex/graphics/pipeline/texture/util.h>
#include <rex/graphics/xenos.h>

namespace rex::graphics::texture_replacement {

// The base level of a guest texture as blocks: untiled, endian swapped,
// without padding, row_count rows of row_pitch bytes.
struct GuestLevel {
  std::vector<uint8_t> blocks;
  uint32_t row_pitch = 0;
  uint32_t row_count = 0;
};

// Reads the base level of a 1D or 2D (one array slice) guest texture from
// guest memory, starting at the base address. Returns false if the layout is
// not one this handles or a block would lie outside the level's data extent.
bool ReadGuestBaseLevel(const uint8_t* guest_base, xenos::TextureFormat format, uint32_t width,
                        uint32_t height, bool tiled, xenos::Endian endianness,
                        const texture_util::TextureGuestLayout& layout, GuestLevel& out);

uint64_t HashGuestLevel(xenos::TextureFormat format, uint32_t width, uint32_t height,
                        const GuestLevel& level);

// Replacement files, <16 hex digits>.dds anywhere under the directory, decoded
// on first use and cached until Evict.
class Store {
 public:
  // Scans the directory; returns the number of replacement files found.
  size_t Initialize(const std::filesystem::path& directory);
  bool IsEnabled() const { return enabled_; }
  bool Has(uint64_t hash) const { return enabled_ && files_.count(hash) != 0; }
  // nullptr if there is no file or it could not be read (logged once).
  const texture_dds::Image* Get(uint64_t hash);
  // Drops the decoded image (after its upload to the host GPU); a later Get
  // reads the file again.
  void Evict(uint64_t hash) { images_.erase(hash); }

 private:
  bool enabled_ = false;
  std::unordered_map<uint64_t, std::filesystem::path> files_;
  std::unordered_map<uint64_t, std::unique_ptr<texture_dds::Image>> images_;
  std::unordered_set<uint64_t> failed_;
};

// Writes each texture once as <hash>.dds and a line about it to textures.txt.
class Dumper {
 public:
  bool Initialize(const std::filesystem::path& directory);
  bool IsEnabled() const { return enabled_; }
  bool IsDumped(uint64_t hash) const { return dumped_.count(hash) != 0; }
  // The DDS format of a guest format whose blocks are stored as they are on
  // the host (the guest format's plain copy), or kFormatUnknown.
  static uint32_t GetDumpFormat(xenos::TextureFormat format);
  // Marks the hash as dumped even if writing fails, so it is tried once.
  void Dump(uint64_t hash, xenos::TextureFormat format, uint32_t width, uint32_t height,
            uint32_t mip_count, uint32_t guest_address, const GuestLevel& level);

 private:
  bool enabled_ = false;
  std::filesystem::path directory_;
  std::unordered_set<uint64_t> dumped_;
};

}  // namespace rex::graphics::texture_replacement
