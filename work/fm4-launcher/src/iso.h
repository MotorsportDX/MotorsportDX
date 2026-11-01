// Xbox 360 disc images (ISO): ports of disc_image_entries and unpack_disc_image in
// outputs/FM4-build/pipeline.py, which the Game data page uses to unpack Disc 1 and Disc 2.
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace fm4 {

struct DiscImageEntry {
  std::string path;          // relative, with '/'
  std::uint64_t offset = 0;  // of the file's bytes in the image
  std::uint64_t size = 0;
  bool directory = false;
};

struct DiscImage {
  std::vector<DiscImageEntry> entries;  // sorted by path
  std::string error;                    // empty when the image was read
};

DiscImage ReadDiscImage(const std::filesystem::path& image);

// Writes entries of ReadDiscImage(image) under out; done grows by each chunk's size. Stops with
// an error when cancel is set.
std::string UnpackDiscImage(const std::filesystem::path& image, const std::vector<DiscImageEntry>& entries,
                            const std::filesystem::path& out, std::atomic<std::uint64_t>& done,
                            const std::atomic<bool>& cancel);

}  // namespace fm4
