#include <rex/system/shader_inventory.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <system_error>
#include <unordered_set>

#include <xxhash.h>

#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/system/lzx.h>

namespace rex::system::shader_inventory {

namespace {

// The shader storage's layout (PipelineCache::InitializeShaderStorage and
// PipelineCache::ShaderStoredHeader): 'XESH', the big-endian version, then per
// shader its key, its dword count with the type in the top bit (1 for pixel
// shaders) and the big-endian microcode. Change these with them.
constexpr uint32_t kStorageMagic = 0x48534558;
constexpr uint32_t kStorageVersion = 0x20201219;
constexpr uint32_t kPixelTypeBit = UINT32_C(1) << 31;

// Zip records.
constexpr uint32_t kZipEndOfDirectory = 0x06054B50;
constexpr uint32_t kZipDirectoryEntry = 0x02014B50;
constexpr uint32_t kZipLocalHeader = 0x04034B50;
constexpr uint16_t kZipStored = 0;
constexpr uint16_t kZipXMemCompress = 21;
// XMemCompress's LZX window.
constexpr uint32_t kLzxWindow = 0x20000;

uint32_t ReadBe32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
uint16_t ReadBe16(const uint8_t* p) { return uint16_t((p[0] << 8) | p[1]); }
uint32_t ReadLe32(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
uint16_t ReadLe16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }

uint32_t Crc32(const uint8_t* data, size_t size) {
  static const std::array<uint32_t, 256> table = [] {
    std::array<uint32_t, 256> t{};
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t c = i;
      for (int k = 0; k < 8; ++k) {
        c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      }
      t[i] = c;
    }
    return t;
  }();
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < size; ++i) {
    crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
  }
  return crc ^ 0xFFFFFFFFu;
}

bool ReadWholeFile(const std::filesystem::path& path, std::vector<uint8_t>& data) {
  FILE* file = rex::filesystem::OpenFile(path, "rb");
  if (!file) {
    return false;
  }
  rex::filesystem::Seek(file, 0, SEEK_END);
  int64_t size = rex::filesystem::Tell(file);
  rex::filesystem::Seek(file, 0, SEEK_SET);
  bool ok = size >= 0;
  if (ok) {
    data.resize(size_t(size));
    ok = !size || fread(data.data(), size_t(size), 1, file) == 1;
  }
  fclose(file);
  return ok;
}

bool ReadAt(FILE* file, uint64_t offset, void* out, size_t size) {
  return rex::filesystem::Seek(file, int64_t(offset), SEEK_SET) && fread(out, size, 1, file) == 1;
}

// An XMemCompress stream: frames of [0xFF, the frame's uncompressed size (short
// frames only)], the compressed size, the LZX data; a zero size ends it.
bool InflateXMemCompress(const uint8_t* data, size_t size, std::vector<uint8_t>& out) {
  std::vector<uint8_t> stream;
  stream.reserve(size);
  size_t i = 0;
  while (i + 2 <= size) {
    if (data[i] == 0xFF) {
      i += 3;
      if (i + 2 > size) {
        break;
      }
    }
    uint16_t length = ReadBe16(data + i);
    i += 2;
    if (!length || i + length > size) {
      break;
    }
    stream.insert(stream.end(), data + i, data + i + length);
    i += length;
  }
  return lzx_decompress(stream.data(), stream.size(), out.data(), out.size(), kLzxWindow, nullptr,
                        0) == 0;
}

class Inventory {
 public:
  enum SourceClass { kFiles, kArchives, kImage, kSourceClassCount };

  // Scans data for shader containers (big-endian, as the title loads them) and
  // records the microcode of every pixel shader.
  void Scan(const uint8_t* data, size_t size, SourceClass source_class) {
    size_t i = 0;
    while (i + 36 <= size) {
      const void* found = std::memchr(data + i, 0x10, size - i - 35);
      if (!found) {
        return;
      }
      i = size_t(static_cast<const uint8_t*>(found) - data);
      if ((i & 3) || data[i + 1] != 0x2A || data[i + 2] != 0x11 || i + 36 > size) {
        ++i;
        continue;
      }
      const uint8_t* c = data + i;
      uint32_t flags = ReadBe32(c);
      uint32_t vertex_size = ReadBe32(c + 4);
      uint32_t pixel_size = ReadBe32(c + 8);
      uint64_t container_size = uint64_t(vertex_size) + pixel_size;
      if ((flags & 0xFFFFFF00u) != 0x102A1100u || ReadBe32(c + 28) || ReadBe32(c + 32) ||
          container_size > size - i) {
        i += 4;
        continue;
      }
      if (!(flags & 1)) {
        AddPixelShader(c, size_t(container_size), source_class);
      }
      i += std::max(size_t(container_size), size_t(4));
    }
  }

  const std::map<uint64_t, std::vector<uint8_t>>& shaders() const { return shaders_; }
  size_t containers() const { return containers_; }
  size_t unique(SourceClass source_class) const { return unique_[source_class].size(); }
  bool from(SourceClass source_class, uint64_t key) const {
    return unique_[source_class].count(key) != 0;
  }

 private:
  void AddPixelShader(const uint8_t* c, size_t size, SourceClass source_class) {
    // The microcode follows the vertex part, at the offset and with the size
    // that the shader's own header at offset 24 names.
    uint32_t vertex_size = ReadBe32(c + 4);
    uint32_t header_offset = ReadBe32(c + 24);
    if (uint64_t(header_offset) + 8 > size) {
      return;
    }
    uint64_t start = uint64_t(vertex_size) + ReadBe32(c + header_offset);
    uint32_t length = ReadBe32(c + header_offset + 4);
    if (!length || (length & 3) || start + length > size) {
      return;
    }
    const uint8_t* code = c + start;
    uint64_t key = XXH3_64bits(code, length);
    ++containers_;
    unique_[source_class].insert(key);
    if (!shaders_.count(key)) {
      shaders_.emplace(key, std::vector<uint8_t>(code, code + length));
    }
  }

  std::map<uint64_t, std::vector<uint8_t>> shaders_;
  std::array<std::unordered_set<uint64_t>, kSourceClassCount> unique_;
  size_t containers_ = 0;
};

// Reads the .fxobj entries of a zip archive.
void ScanArchive(const std::filesystem::path& path, Inventory& inventory, Result& result) {
  FILE* file = rex::filesystem::OpenFile(path, "rb");
  if (!file) {
    ++result.errors;
    return;
  }
  rex::filesystem::Seek(file, 0, SEEK_END);
  int64_t file_size = rex::filesystem::Tell(file);
  // The end of the central directory, within the last 64 KB and its record.
  size_t tail_size = size_t(std::min<int64_t>(file_size, 0xFFFF + 22));
  std::vector<uint8_t> tail(tail_size);
  if (tail_size < 22 || !ReadAt(file, uint64_t(file_size) - tail_size, tail.data(), tail_size)) {
    fclose(file);
    ++result.errors;
    return;
  }
  size_t end = tail_size - 22 + 1;
  bool found = false;
  while (end-- > 0) {
    if (ReadLe32(tail.data() + end) == kZipEndOfDirectory) {
      found = true;
      break;
    }
  }
  if (!found) {
    fclose(file);
    ++result.errors;
    return;
  }
  uint32_t directory_size = ReadLe32(tail.data() + end + 12);
  uint32_t directory_offset = ReadLe32(tail.data() + end + 16);
  std::vector<uint8_t> directory(directory_size);
  if (!directory_size ||
      uint64_t(directory_offset) + directory_size > uint64_t(file_size) ||
      !ReadAt(file, directory_offset, directory.data(), directory_size)) {
    fclose(file);
    ++result.errors;
    return;
  }
  ++result.archives;
  std::vector<uint8_t> compressed, data;
  for (size_t i = 0; i + 46 <= directory.size();) {
    const uint8_t* e = directory.data() + i;
    if (ReadLe32(e) != kZipDirectoryEntry) {
      ++result.errors;
      break;
    }
    uint16_t method = ReadLe16(e + 10);
    uint32_t crc = ReadLe32(e + 16);
    uint32_t compressed_size = ReadLe32(e + 20);
    uint32_t size = ReadLe32(e + 24);
    uint16_t name_length = ReadLe16(e + 28);
    uint16_t extra_length = ReadLe16(e + 30);
    uint16_t comment_length = ReadLe16(e + 32);
    uint32_t local_offset = ReadLe32(e + 42);
    if (i + 46 + name_length > directory.size()) {
      ++result.errors;
      break;
    }
    std::string name(reinterpret_cast<const char*>(e + 46), name_length);
    i += 46 + size_t(name_length) + extra_length + comment_length;
    std::transform(name.begin(), name.end(), name.begin(),
                   [](char ch) { return char(std::tolower(uint8_t(ch))); });
    if (name.size() < 6 || name.compare(name.size() - 6, 6, ".fxobj")) {
      continue;
    }
    ++result.entries;
    uint8_t local[30];
    if (!ReadAt(file, local_offset, local, sizeof(local)) || ReadLe32(local) != kZipLocalHeader) {
      ++result.errors;
      continue;
    }
    uint64_t data_offset =
        uint64_t(local_offset) + 30 + ReadLe16(local + 26) + ReadLe16(local + 28);
    compressed.resize(compressed_size);
    if (compressed_size && !ReadAt(file, data_offset, compressed.data(), compressed_size)) {
      ++result.errors;
      continue;
    }
    if (method == kZipStored) {
      data.swap(compressed);
    } else if (method == kZipXMemCompress) {
      data.resize(size);
      if (!InflateXMemCompress(compressed.data(), compressed.size(), data)) {
        ++result.errors;
        continue;
      }
    } else {
      ++result.errors;
      continue;
    }
    if (data.size() != size || Crc32(data.data(), data.size()) != crc) {
      ++result.errors;
      continue;
    }
    inventory.Scan(data.data(), data.size(), Inventory::kArchives);
  }
  fclose(file);
}

bool HasExtension(const std::filesystem::path& path, const char* extension) {
  std::string text = path.extension().string();
  std::transform(text.begin(), text.end(), text.begin(),
                 [](char ch) { return char(std::tolower(uint8_t(ch))); });
  return text == extension;
}

}  // namespace

bool Run(const Sources& sources, const std::filesystem::path& storage_path, Result& result,
         const Progress& progress, const std::filesystem::path& keys_path) {
  auto start = std::chrono::steady_clock::now();
  result = Result();

  std::vector<std::filesystem::path> files, archives;
  for (const auto& root : sources.roots) {
    std::error_code error;
    if (!std::filesystem::is_directory(root, error)) {
      continue;
    }
    for (auto it = std::filesystem::recursive_directory_iterator(
             root, std::filesystem::directory_options::skip_permission_denied, error);
         !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error)) {
      if (!it->is_regular_file(error)) {
        continue;
      }
      if (HasExtension(it->path(), ".fxobj")) {
        files.push_back(it->path());
      } else if (HasExtension(it->path(), ".zip")) {
        archives.push_back(it->path());
      }
    }
  }
  std::sort(files.begin(), files.end());
  std::sort(archives.begin(), archives.end());

  Inventory inventory;
  size_t total = files.size() + archives.size() + (sources.image ? 1 : 0);
  size_t done = 0;
  std::vector<uint8_t> data;
  for (const auto& path : files) {
    if (ReadWholeFile(path, data)) {
      ++result.files;
      inventory.Scan(data.data(), data.size(), Inventory::kFiles);
    } else {
      ++result.errors;
    }
    if (progress) {
      progress(++done, total, rex::path_to_utf8(path));
    }
  }
  for (const auto& path : archives) {
    ScanArchive(path, inventory, result);
    if (progress) {
      progress(++done, total, rex::path_to_utf8(path));
    }
  }
  if (sources.image) {
    inventory.Scan(sources.image, sources.image_size, Inventory::kImage);
    if (progress) {
      progress(++done, total, "image");
    }
  }
  result.containers = inventory.containers();
  result.unique = inventory.shaders().size();
  result.unique_files = inventory.unique(Inventory::kFiles);
  result.unique_archives = inventory.unique(Inventory::kArchives);
  result.unique_image = inventory.unique(Inventory::kImage);

  if (!keys_path.empty()) {
    if (FILE* keys = rex::filesystem::OpenFile(keys_path, "wb")) {
      for (const auto& [key, code] : inventory.shaders()) {
        const char* source_class = inventory.from(Inventory::kFiles, key)      ? "files"
                                   : inventory.from(Inventory::kArchives, key) ? "archives"
                                                                               : "image";
        fprintf(keys, "%016llX %s\n", static_cast<unsigned long long>(key), source_class);
      }
      fclose(keys);
    }
  }

  // Read the storage's keys, validating the records as the start-up does, and
  // append what it lacks after its last valid record.
  std::error_code error;
  std::filesystem::create_directories(storage_path.parent_path(), error);
  FILE* storage = rex::filesystem::OpenFile(storage_path, "a+b");
  if (!storage) {
    REXSYS_ERROR("Shader inventory: failed to open the shader storage {}",
                 rex::path_to_utf8(storage_path));
    return false;
  }
  rex::filesystem::Seek(storage, 0, SEEK_SET);
  std::unordered_set<uint64_t> stored;
  uint32_t header[2];
  uint64_t valid_bytes = 0;
  if (fread(header, sizeof(header), 1, storage) && header[0] == kStorageMagic &&
      ReadBe32(reinterpret_cast<const uint8_t*>(&header[1])) == kStorageVersion) {
    valid_bytes = sizeof(header);
    std::vector<uint8_t> code;
    for (;;) {
      uint8_t record[12];
      if (!fread(record, sizeof(record), 1, storage)) {
        break;
      }
      uint64_t key;
      uint32_t word;
      std::memcpy(&key, record, sizeof(key));
      std::memcpy(&word, record + 8, sizeof(word));
      code.resize(size_t(word & ~kPixelTypeBit) * 4);
      if (!code.empty() && !fread(code.data(), code.size(), 1, storage)) {
        break;
      }
      if (XXH3_64bits(code.data(), code.size()) != key) {
        break;
      }
      valid_bytes += sizeof(record) + code.size();
      if (word & kPixelTypeBit) {
        stored.insert(key);
      }
    }
  }
  result.stored = stored.size();
  if (!valid_bytes) {
    rex::filesystem::TruncateStdioFile(storage, 0);
    // The magic in host order, the version big-endian.
    uint8_t new_header[8];
    std::memcpy(new_header, &kStorageMagic, 4);
    for (int k = 0; k < 4; ++k) {
      new_header[4 + k] = uint8_t(kStorageVersion >> (24 - 8 * k));
    }
    rex::filesystem::Seek(storage, 0, SEEK_END);
    fwrite(new_header, sizeof(new_header), 1, storage);
  } else {
    rex::filesystem::TruncateStdioFile(storage, valid_bytes);
  }
  rex::filesystem::Seek(storage, 0, SEEK_END);
  for (const auto& [key, code] : inventory.shaders()) {
    if (stored.count(key)) {
      continue;
    }
    uint32_t word = uint32_t(code.size() / 4) | kPixelTypeBit;
    fwrite(&key, sizeof(key), 1, storage);
    fwrite(&word, sizeof(word), 1, storage);
    fwrite(code.data(), code.size(), 1, storage);
    ++result.appended;
  }
  fclose(storage);

  result.milliseconds =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  REXSYS_INFO(
      "Shader inventory: {} pixel shaders ({} containers; {} from files, {} from archives, {} from "
      "the image) in {} files and {} archives ({} entries, {} errors); the storage held {}, {} "
      "appended; {:.1f} ms",
      result.unique, result.containers, result.unique_files, result.unique_archives,
      result.unique_image, result.files, result.archives, result.entries, result.errors,
      result.stored, result.appended, result.milliseconds);
  return true;
}

}  // namespace rex::system::shader_inventory
