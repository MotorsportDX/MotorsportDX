/**
 * @file        graphics/pipeline/texture/content_hash.h
 * @brief       Content hash of a guest texture, the name of its dump and
 *              replacement files
 *
 * XXH3 64 over a small header (hash version, guest format, width, height) and
 * the base level's blocks row by row, after untiling and endian swapping, so
 * the hash depends only on the texels: not on the guest address, the pitch,
 * the tiling, the padding or the endianness of the fetch constant. Mips are not
 * hashed. Header only, so CPU tests can include it (needs xxhash.h).
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// Inline, as rex/hash.h uses it, so CPU tests need no xxHash library.
#ifndef XXH_INLINE_ALL
#define XXH_INLINE_ALL
#endif
#include <xxhash.h>

namespace rex::graphics::texture_content_hash {

// Bump only when the hashed data changes; every texture pack is keyed by it.
constexpr uint32_t kVersion = 1;

class Hasher {
 public:
  // guest_format is the xenos::TextureFormat value; width and height are in
  // texels.
  Hasher(uint32_t guest_format, uint32_t width, uint32_t height) {
    XXH3_64bits_reset(&state_);
    const uint32_t header[4] = {kVersion, guest_format, width, height};
    XXH3_64bits_update(&state_, header, sizeof(header));
  }
  // One row of blocks (texels for uncompressed formats), only the bytes inside
  // the texture's width.
  void AddRow(const void* data, size_t size) { XXH3_64bits_update(&state_, data, size); }
  uint64_t Finish() const { return XXH3_64bits_digest(&state_); }

 private:
  XXH3_state_t state_;
};

// 16 lowercase hex digits: the file name stem of dumps and replacements.
inline std::string HashName(uint64_t hash) {
  static const char kDigits[] = "0123456789abcdef";
  std::string name(16, '0');
  for (int i = 15; i >= 0; --i) {
    name[size_t(i)] = kDigits[hash & 0xF];
    hash >>= 4;
  }
  return name;
}

// Accepts 16 hex digits of either case.
inline bool ParseHashName(std::string_view name, uint64_t& hash_out) {
  if (name.size() != 16) {
    return false;
  }
  uint64_t hash = 0;
  for (char c : name) {
    uint32_t digit;
    if (c >= '0' && c <= '9') {
      digit = uint32_t(c - '0');
    } else if (c >= 'a' && c <= 'f') {
      digit = uint32_t(c - 'a' + 10);
    } else if (c >= 'A' && c <= 'F') {
      digit = uint32_t(c - 'A' + 10);
    } else {
      return false;
    }
    hash = (hash << 4) | digit;
  }
  hash_out = hash;
  return true;
}

}  // namespace rex::graphics::texture_content_hash
