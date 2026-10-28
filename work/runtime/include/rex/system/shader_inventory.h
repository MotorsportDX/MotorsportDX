#pragma once
// The inventory of the title's pixel shaders on the user's disc and installed
// content (part 3 of docs/analysis/d3d-hle-n6.md 19.3.2, increment 1.3). It
// reads the shader containers of the loose .fxobj files, the .fxobj entries of
// the title's zip archives (stored or XMemCompress LZX) and the loaded
// executable's image, and appends every pixel shader that the shader storage
// does not hold yet to the storage (<cache_root>\shaders\shareable\<title>.xsh),
// so the start-up translates it before the title draws with it. Vertex shaders
// are left out: the XDK rewrites their fetch instructions at draw time, so their
// keys exist only once the title has drawn with them.
//
// The keys are the runtime's: XXH3_64 of the big-endian microcode, as
// PipelineCache::LoadShader computes them. Nothing the inventory reads or
// writes leaves the user's machine.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace rex::system::shader_inventory {

struct Sources {
  // Directories searched recursively for .fxobj files and .zip archives.
  std::vector<std::filesystem::path> roots;
  // The loaded executable's image in host memory (its containers are in the
  // data sections), or null.
  const uint8_t* image = nullptr;
  size_t image_size = 0;
};

struct Result {
  size_t files = 0;
  size_t archives = 0;
  // The .fxobj entries read from the archives.
  size_t entries = 0;
  // Entries that could not be read (unknown method, a failed decompression or
  // a CRC mismatch).
  size_t errors = 0;
  // Pixel shader containers found, and their unique keys.
  size_t containers = 0;
  size_t unique = 0;
  // Unique keys per source class (disc files, archives, the image).
  size_t unique_files = 0;
  size_t unique_archives = 0;
  size_t unique_image = 0;
  // Pixel shaders the storage held before, and those appended.
  size_t stored = 0;
  size_t appended = 0;
  // Interpolator words written beside the storage.
  size_t inputs = 0;
  double milliseconds = 0.0;
};

// Called after each source with the number done, the total and the source's
// path, for the offline pass's progress lines.
using Progress = std::function<void(size_t done, size_t total, const std::string& current)>;

// Runs the inventory and appends the new pixel shaders to the storage file. A
// storage file with another magic or version is rewritten with the current
// header, as PipelineCache::InitializeShaderStorage does. If keys_path is not
// empty, writes the inventory's keys there, one "<key> <source class>" line
// each (for the gates' check against the storage; game-derived, keep it out of
// the repository).
bool Run(const Sources& sources, const std::filesystem::path& storage_path, Result& result,
         const Progress& progress = nullptr,
         const std::filesystem::path& keys_path = std::filesystem::path());

// The inventory also writes, beside the storage (<title>.inputs), each pixel
// shader's interpolator word from its container's header: the interpolators it
// reads in the low half and those it samples at the pixel center in the high
// half (the others at the centroid with MSAA). The start predicts the
// modifications of a shader that no stored pipeline names from it. A shader in
// several containers may have several words.
std::filesystem::path InputsPath(const std::filesystem::path& storage_path);
bool ReadInputs(const std::filesystem::path& storage_path,
                std::unordered_multimap<uint64_t, uint32_t>& inputs);

}  // namespace rex::system::shader_inventory
