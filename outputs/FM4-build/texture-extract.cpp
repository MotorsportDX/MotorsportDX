// Helper of texture_extract.py: hashes and dumps a texture found in the game
// files exactly as the runtime hashes and dumps it in guest memory. It is
// built with the runtime's own texture replacement code
// (<BuildRoot>/runtime-source/src/graphics/pipeline/texture/replacement.cpp:
// ReadGuestBaseLevel, HashGuestLevel and Dumper) and links the installed
// rexruntime for the guest texture layout, the tiling and the format table, so
// a file it writes has the name and the contents of the runtime's dump of the
// same texture. See docs/analysis/texture-replacement.md, "Offline extraction".

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <vector>

#include <rex/graphics/pipeline/texture/replacement.h>
#include <rex/graphics/pipeline/texture/util.h>
#include <rex/graphics/xenos.h>

namespace {

namespace replacement = rex::graphics::texture_replacement;
namespace texture_util = rex::graphics::texture_util;
namespace xenos = rex::graphics::xenos;

std::unique_ptr<replacement::Dumper> dumper;

}  // namespace

// Starts a dump into directory (UTF-8), as texture_dump_path does: files already
// there are not written again. Returns 0, or -1 if the directory is unusable.
extern "C" __declspec(dllexport) int fm4_texture_dump_open(const char* directory) {
  dumper = std::make_unique<replacement::Dumper>();
  return dumper->Initialize(std::filesystem::path(reinterpret_cast<const char8_t*>(directory)))
             ? 0
             : -1;
}

// The base level of a 2D texture whose data (base level first, as at the
// guest base address) is data[0, size). format and endianness are the
// xenos::TextureFormat and xenos::Endian values, pitch the row pitch in texels
// (the fetch constant's pitch times 32). Writes the hash to hash_out and, with
// an open dump, dumps the texture once per hash.
// Returns 0 when hashed, -1 for a format without blocks, -2 if data is shorter
// than the base level's extent, -3 if the base level could not be read.
extern "C" __declspec(dllexport) int fm4_texture_hash(const uint8_t* data, size_t size,
                                                      uint32_t format, uint32_t width,
                                                      uint32_t height, uint32_t pitch,
                                                      uint32_t tiled, uint32_t endianness,
                                                      uint32_t packed_mips, uint32_t mip_count,
                                                      uint64_t* hash_out) {
  auto guest_format = xenos::TextureFormat(format);
  if (!rex::graphics::FormatInfo::Get(format)->bytes_per_block()) {
    return -1;
  }
  uint32_t max_level = mip_count ? mip_count - 1 : 0;
  texture_util::TextureGuestLayout layout = texture_util::GetGuestTextureLayout(
      xenos::DataDimension::k2DOrStacked, (pitch + 31) / 32, width, height, 1, tiled != 0,
      guest_format, packed_mips != 0, true, max_level);
  // ReadGuestBaseLevel reads the whole extent, which may end in the padding of
  // the last tile.
  if (layout.base.level_data_extent_bytes > size) {
    return -2;
  }
  replacement::GuestLevel level;
  if (!replacement::ReadGuestBaseLevel(data, guest_format, width, height, tiled != 0,
                                       xenos::Endian(endianness), layout, level)) {
    return -3;
  }
  uint64_t hash = replacement::HashGuestLevel(guest_format, width, height, level);
  *hash_out = hash;
  if (dumper && dumper->IsEnabled() && !dumper->IsDumped(hash)) {
    dumper->Dump(hash, guest_format, width, height, max_level + 1, 0, level);
  }
  return 0;
}
