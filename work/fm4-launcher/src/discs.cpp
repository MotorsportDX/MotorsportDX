#include "discs.h"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <map>

namespace fm4 {

namespace fs = std::filesystem;

std::string Sha256File(const fs::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) throw std::runtime_error("Unable to read " + Utf8(path));
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
      BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) < 0)
    throw std::runtime_error("SHA-256 is not available");
  std::vector<char> buffer(1 << 20);
  while (stream) {
    stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const auto count = stream.gcount();
    if (count > 0)
      BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()), static_cast<ULONG>(count), 0);
  }
  unsigned char digest[32];
  BCryptFinishHash(hash, digest, sizeof(digest), 0);
  BCryptDestroyHash(hash);
  BCryptCloseAlgorithmProvider(algorithm, 0);
  static const char digits[] = "0123456789abcdef";
  std::string text;
  for (unsigned char byte : digest) {
    text += digits[byte >> 4];
    text += digits[byte & 15];
  }
  return text;
}

namespace {

std::vector<fs::path> Disc2Packages(const std::string& disc2) {
  const fs::path packages = PathFromUtf8(disc2) / "content" / "0000000000000000" / "4d530910" / "00000002";
  std::vector<fs::path> paths;
  for (int index = 1; index <= 4; ++index)
    paths.push_back(packages / ("4d5309100000000" + std::to_string(index)));
  return paths;
}

struct Header {
  std::string magic;
  uint32_t content_type = 0;
  uint32_t title_id = 0;
};

uint32_t BigEndian(const unsigned char* bytes) {
  return (uint32_t(bytes[0]) << 24) | (uint32_t(bytes[1]) << 16) | (uint32_t(bytes[2]) << 8) | bytes[3];
}

// package_header of pipeline.py: the magic, content type and title ID of an STFS package.
bool PackageHeader(const LauncherData& data, const fs::path& path, Header& header) {
  const Json& content = data.json.at("content");
  const size_t type_offset = content.at("content_type_offset").get<size_t>();
  const size_t title_offset = content.at("title_id_offset").get<size_t>();
  std::ifstream stream(path, std::ios::binary);
  std::vector<unsigned char> bytes(title_offset + 4);
  stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (static_cast<size_t>(stream.gcount()) < bytes.size()) return false;
  const std::string magic(bytes.begin(), bytes.begin() + 4);
  bool known = false;
  for (const auto& item : content.at("magics")) known = known || item.get<std::string>() == magic;
  if (!known) return false;
  header.magic = magic;
  header.content_type = BigEndian(&bytes[type_offset]);
  header.title_id = BigEndian(&bytes[title_offset]);
  return true;
}

std::string Hex(uint32_t value) {
  char text[16];
  std::snprintf(text, sizeof(text), "%08X", value);
  return text;
}

// content_error of pipeline.py.
std::string ContentError(const LauncherData& data, const fs::path& package) {
  Header header;
  if (!PackageHeader(data, package, header)) return "Not an STFS content package: " + Utf8(package);
  const uint32_t title = data.json.at("content").at("title_id").get<uint32_t>();
  const uint32_t type = data.json.at("content").at("content_type").get<uint32_t>();
  if (header.title_id != title)
    return "Content package is for title " + Hex(header.title_id) + ", not FM4 (" + Hex(title) +
           "): " + Utf8(package);
  if (header.content_type != type)
    return "Content package type " + Hex(header.content_type) +
           " is not FM4 marketplace content (" + Hex(type) + "): " + Utf8(package);
  return {};
}

std::vector<std::wstring> NormalizedParts(const fs::path& path) {
  std::vector<std::wstring> parts;
  for (const auto& part : path) {
    std::wstring text = part.native();
    for (auto& c : text) c = static_cast<wchar_t>(std::towlower(c));
    parts.push_back(text);
  }
  return parts;
}

std::wstring Lower(std::wstring text) {
  for (auto& c : text) c = static_cast<wchar_t>(std::towlower(c));
  return text;
}

}  // namespace

DiscCheck CheckDisc1(const LauncherData& data, const std::string& disc1) {
  DiscCheck check;
  if (disc1.empty()) {
    check.error = "Choose the extracted Disc 1 (Play Disc) folder.";
    return check;
  }
  check.path = ResolvePath(disc1);
  const fs::path root = PathFromUtf8(check.path);
  std::map<std::string, std::string> hashes;
  try {
    for (const auto& name : data.json.at("releases").begin()->at("executables").items()) {
      const fs::path executable = root / name.key();
      if (!fs::is_regular_file(executable)) {
        check.error = "Missing executable: " + Utf8(executable) + ".";
        return check;
      }
      hashes[name.key()] = Sha256File(executable);
    }
  } catch (const std::exception& error) {
    check.error = error.what();
    return check;
  }
  std::vector<std::string> supported;
  for (const auto& [release, info] : data.json.at("releases").items()) {
    bool match = true;
    for (const auto& [name, hash] : info.at("executables").items())
      match = match && hashes[name] == hash.get<std::string>();
    if (match) check.release = release;
    supported.push_back(release + " (media ID " + info.at("media_id").get<std::string>() + ")");
  }
  if (check.release.empty()) {
    std::string list;
    for (size_t i = 0; i < supported.size(); ++i) list += (i ? ", " : "") + supported[i];
    check.error = "Unsupported executables in " + check.path +
                  ": the three must be those of one supported release: " + list + ".";
    return check;
  }
  if (!fs::is_directory(root / "Media") || !fs::is_regular_file(root / "Database.xmplr"))
    check.error = "Disc 1 must contain the extracted game assets (Media and Database.xmplr), not "
                  "just the three XEX files.";
  return check;
}

DiscCheck CheckDisc2(const LauncherData& data, const std::string& disc2) {
  DiscCheck check;
  if (disc2.empty()) return check;
  check.path = ResolvePath(disc2);
  for (const auto& package : Disc2Packages(check.path)) {
    if (!fs::is_regular_file(package)) {
      check.error = "Missing Disc 2 content package: " + Utf8(package);
      return check;
    }
    check.error = ContentError(data, package);
    if (!check.error.empty()) return check;
  }
  return check;
}

ContentCheck CheckContent(const LauncherData& data, const std::string& disc2,
                          const std::vector<std::string>& content) {
  ContentCheck check;
  std::vector<fs::path> packages;
  if (!disc2.empty()) {
    for (const auto& package : Disc2Packages(ResolvePath(disc2))) {
      check.error = ContentError(data, package);
      if (!check.error.empty()) return check;
      packages.push_back(package);
    }
  }
  for (const auto& source_text : content) {
    const fs::path source = PathFromUtf8(ResolvePath(source_text));
    std::error_code error;
    if (fs::is_regular_file(source, error)) {
      check.error = ContentError(data, source);
      if (!check.error.empty()) return check;
      packages.push_back(source);
    } else if (fs::is_directory(source, error)) {
      std::vector<fs::path> files;
      for (auto it = fs::recursive_directory_iterator(
               source, fs::directory_options::skip_permission_denied, error);
           !error && it != fs::recursive_directory_iterator(); it.increment(error))
        files.push_back(it->path());
      std::sort(files.begin(), files.end(), [](const fs::path& a, const fs::path& b) {
        return NormalizedParts(a) < NormalizedParts(b);
      });
      int found = 0;
      for (const auto& path : files) {
        Header header;
        if (!fs::is_regular_file(path, error) || !PackageHeader(data, path, header)) continue;
        const std::string skipped = ContentError(data, path);
        if (!skipped.empty()) {
          check.skipped.push_back(skipped);
        } else {
          packages.push_back(path);
          ++found;
        }
      }
      if (!found) {
        check.error = "No FM4 content packages found in " + Utf8(source);
        return check;
      }
    } else {
      check.error = "Content path not found: " + Utf8(source);
      return check;
    }
  }
  std::map<std::wstring, fs::path> names;
  for (const auto& package : packages) {
    const auto [it, inserted] = names.emplace(Lower(package.filename().native()), package);
    if (!inserted && it->second != package) {
      check.error = "Two content packages share the name " + Utf8(package.filename()) + ": " +
                    Utf8(it->second) + " and " + Utf8(package);
      return check;
    }
    if (inserted) {
      std::error_code error;
      check.packages.push_back({package, fs::file_size(package, error)});
    }
  }
  return check;
}

}  // namespace fm4
