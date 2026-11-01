#include "iso.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <set>

#include "plan.h"

namespace fm4 {

namespace fs = std::filesystem;

namespace {

// The constants of pipeline.py's GDFX reader.
constexpr char kMagic[] = "MICROSOFT*XBOX*MEDIA";
constexpr std::uint64_t kOffsets[] = {0x00000000, 0x0000FB20, 0x00020600, 0x02080000, 0x0FD90000};
constexpr std::uint64_t kSector = 2048;
constexpr std::uint8_t kDirectory = 0x10;
constexpr std::uint64_t kMaxTable = 32 << 20;

std::uint32_t Little32(const unsigned char* bytes) {
  return std::uint32_t(bytes[0]) | (std::uint32_t(bytes[1]) << 8) | (std::uint32_t(bytes[2]) << 16) |
         (std::uint32_t(bytes[3]) << 24);
}

std::uint16_t Little16(const unsigned char* bytes) { return std::uint16_t(bytes[0] | (bytes[1] << 8)); }

std::string Lower(std::string text) {
  for (auto& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return text;
}

bool SafeName(const std::string& name) {
  if (name.empty() || name == "." || name == ".." || name.back() == ' ' || name.back() == '.') return false;
  for (unsigned char c : name)
    if (c < 32 || c > 126 || std::strchr("<>:\"/\\|?*", c)) return false;
  return true;
}

}  // namespace

DiscImage ReadDiscImage(const fs::path& image) {
  DiscImage result;
  std::error_code error;
  const std::uint64_t size = fs::file_size(image, error);
  std::ifstream stream(image, std::ios::binary);
  if (error || !stream) {
    result.error = "Unable to read " + Utf8(image);
    return result;
  }
  const auto damaged = [&](const std::string& what) {
    result.entries.clear();
    result.error = "Damaged disc image (" + what + "): " + Utf8(image);
    return result;
  };
  const auto read = [&](std::uint64_t offset, std::uint64_t length, std::vector<unsigned char>& bytes) {
    if (offset + length > size) return false;
    bytes.resize(length);
    stream.clear();
    stream.seekg(static_cast<std::streamoff>(offset));
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(length));
    return static_cast<std::uint64_t>(stream.gcount()) == length;
  };
  std::vector<unsigned char> bytes;
  std::uint64_t base = 0;
  bool found = false;
  for (const std::uint64_t offset : kOffsets) {
    if (offset + 33 * kSector <= size && read(offset + 32 * kSector, 28, bytes) &&
        std::memcmp(bytes.data(), kMagic, 20) == 0) {
      base = offset;
      found = true;
      break;
    }
  }
  if (!found) {
    result.error = "Not an Xbox 360 disc image: " + Utf8(image);
    return result;
  }
  struct Directory {
    std::string path;
    std::uint64_t sector, length;
  };
  std::vector<Directory> directories{{"", Little32(&bytes[20]), Little32(&bytes[24])}};
  std::set<std::string> names;
  std::set<std::uint64_t> tables;
  std::vector<unsigned char> table;
  while (!directories.empty()) {
    const Directory directory = directories.back();
    directories.pop_back();
    if (!directory.length) continue;
    if (directory.length > kMaxTable || !tables.insert(directory.sector).second) return damaged("a directory table");
    if (!read(base + directory.sector * kSector, directory.length, table)) return damaged("a table beyond its end");
    std::vector<std::uint16_t> nodes{0};
    std::set<std::uint16_t> visited;
    while (!nodes.empty()) {
      const std::uint16_t ordinal = nodes.back();
      nodes.pop_back();
      const size_t at = size_t(ordinal) * 4;
      if (!visited.insert(ordinal).second || at + 14 > table.size()) return damaged("a directory entry");
      const unsigned char* p = &table[at];
      const std::uint16_t left = Little16(p), right = Little16(p + 2);
      const std::uint64_t sector = Little32(p + 4), length = Little32(p + 8);
      const std::uint8_t attributes = p[12], name_length = p[13];
      if (at + 14 + name_length > table.size()) {
        result.error = "Unsupported file name in the disc image " + Utf8(image);
        result.entries.clear();
        return result;
      }
      const std::string name(reinterpret_cast<const char*>(p + 14), name_length);
      if (!SafeName(name)) {
        result.error = "Unsupported file name '" + name + "' in the disc image " + Utf8(image);
        result.entries.clear();
        return result;
      }
      const std::string path = directory.path.empty() ? name : directory.path + "/" + name;
      if (!names.insert(Lower(path)).second) return damaged("two entries named " + path);
      DiscImageEntry entry{path, 0, 0, (attributes & kDirectory) != 0};
      if (entry.directory) {
        directories.push_back({path, sector, length});
      } else {
        entry.offset = base + sector * kSector;
        entry.size = length;
        if (entry.offset + entry.size > size) return damaged(path + " beyond its end");
      }
      result.entries.push_back(std::move(entry));
      if (right) nodes.push_back(right);
      if (left) nodes.push_back(left);
    }
  }
  std::sort(result.entries.begin(), result.entries.end(),
            [](const DiscImageEntry& a, const DiscImageEntry& b) { return a.path < b.path; });
  return result;
}

std::string UnpackDiscImage(const fs::path& image, const std::vector<DiscImageEntry>& entries, const fs::path& out,
                            std::atomic<std::uint64_t>& done, const std::atomic<bool>& cancel) {
  std::ifstream stream(image, std::ios::binary);
  if (!stream) return "Unable to read " + Utf8(image);
  std::vector<char> buffer(8 << 20);
  for (const auto& entry : entries) {
    const fs::path target = out / PathFromUtf8(entry.path);
    std::error_code error;
    fs::create_directories(entry.directory ? target : target.parent_path(), error);
    if (error) return "Unable to create " + Utf8(entry.directory ? target : target.parent_path());
    if (entry.directory) continue;
    std::ofstream output(target, std::ios::binary | std::ios::trunc);
    if (!output) return "Unable to write " + Utf8(target);
    stream.clear();
    stream.seekg(static_cast<std::streamoff>(entry.offset));
    for (std::uint64_t remaining = entry.size; remaining;) {
      if (cancel) return "Unpacking stopped.";
      const auto count = static_cast<std::streamsize>(std::min<std::uint64_t>(remaining, buffer.size()));
      stream.read(buffer.data(), count);
      if (stream.gcount() != count) return "The disc image ends inside " + entry.path + ": " + Utf8(image);
      output.write(buffer.data(), count);
      if (!output) return "Unable to write " + Utf8(target) + " (is the drive full?)";
      remaining -= static_cast<std::uint64_t>(count);
      done += static_cast<std::uint64_t>(count);
    }
  }
  return {};
}

}  // namespace fm4
