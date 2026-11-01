#include "discs.h"
#include "iso.h"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <map>
#include <set>

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

std::vector<fs::path> Disc2Packages(const std::string& disc2) {
  const fs::path packages = PathFromUtf8(disc2) / "content" / "0000000000000000" / "4d530910" / "00000002";
  std::vector<fs::path> paths;
  for (int index = 1; index <= 4; ++index)
    paths.push_back(packages / ("4d5309100000000" + std::to_string(index)));
  return paths;
}

namespace {

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

constexpr char kImageFile[] =
    "This is a file, not a folder. For an ISO, use \"Unpack a disc image (ISO)\" below.";

}  // namespace

DiscCheck CheckDisc1(const LauncherData& data, const std::string& disc1) {
  DiscCheck check;
  if (disc1.empty()) {
    check.error = "Choose the extracted Disc 1 (Play Disc) folder.";
    return check;
  }
  check.path = ResolvePath(disc1);
  const fs::path root = PathFromUtf8(check.path);
  if (fs::is_regular_file(root)) {
    check.error = kImageFile;
    return check;
  }
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
  if (fs::is_regular_file(PathFromUtf8(check.path))) {
    check.error = kImageFile;
    return check;
  }
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

DiscUnpack UnpackDisc(const LauncherData& data, const fs::path& discs, const fs::path& image,
                      std::atomic<std::uint64_t>& done, std::atomic<std::uint64_t>& total,
                      const std::atomic<bool>& cancel, const std::function<void(const std::string&)>& started) {
  DiscUnpack result;
  const DiscImage read = ReadDiscImage(image);
  if (!read.error.empty()) {
    result.message = read.error;
    return result;
  }
  const auto lower = [](std::string text) {
    for (auto& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
  };
  std::map<std::string, const DiscImageEntry*> files;
  std::uint64_t bytes = 0;
  for (const auto& entry : read.entries) {
    if (entry.directory) continue;
    files[lower(entry.path)] = &entry;
    bytes += entry.size;
  }
  std::vector<DiscImageEntry> first;
  const auto take = [&](const std::vector<std::string>& paths) {
    first.clear();
    for (const auto& path : paths) {
      const auto it = files.find(lower(path));
      if (it == files.end()) return false;
      first.push_back(*it->second);
    }
    return true;
  };
  std::vector<std::string> packages, executables;
  for (const auto& package : Disc2Packages("")) {
    std::string path = Utf8(package);
    std::replace(path.begin(), path.end(), '\\', '/');
    packages.push_back(path);
  }
  for (const auto& item : data.json.at("releases").begin()->at("executables").items())
    executables.push_back(item.key());
  if (!take(packages)) {
    if (!take(executables)) {
      result.message = Utf8(image.filename()) + " is neither Disc 1 (Play Disc) nor Disc 2 (Content Disc).";
      return result;
    }
    result.disc1 = true;
  }
  const std::string name = result.disc1 ? "disc1" : "disc2", label = result.disc1 ? "Disc 1" : "Disc 2";
  const fs::path folder = discs / name, marker = discs / (name + ".json"), partial = discs / (name + ".partial");
  // The source as pipeline.py records it: st_mtime_ns is the write time in Unix nanoseconds.
  WIN32_FILE_ATTRIBUTE_DATA attributes{};
  GetFileAttributesExW(image.c_str(), GetFileExInfoStandard, &attributes);
  const auto written = static_cast<std::int64_t>((std::uint64_t(attributes.ftLastWriteTime.dwHighDateTime) << 32) |
                                                 attributes.ftLastWriteTime.dwLowDateTime);
  std::error_code error;
  const Json source = {{"image", Utf8(image)},
                       {"size", fs::file_size(image, error)},
                       {"mtime_ns", (written - 116444736000000000LL) * 100}};
  if (fs::is_directory(folder, error) && fs::is_regular_file(marker, error)) {
    try {
      std::ifstream stream(marker, std::ios::binary);
      const Json recorded = Json::parse(stream).at("source");
      if (recorded.at("image") == source.at("image") && recorded.at("size") == source.at("size") &&
          recorded.at("mtime_ns") == source.at("mtime_ns")) {
        result.folder = Utf8(folder);
        result.message = label + ": " + result.folder + ", unpacked before from this image.";
        return result;
      }
    } catch (const std::exception&) {
    }
  }
  fs::create_directories(discs, error);
  fs::remove(marker, error);
  fs::remove_all(partial, error);
  const auto gigabytes = [](std::uintmax_t count) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.1f GB", static_cast<double>(count) / (1ull << 30));
    return std::string(text);
  };
  const auto space = fs::space(discs, error);
  if (!error && space.available < bytes + (1ull << 30)) {
    result.message = "Unpacking " + label + " needs " + gigabytes(bytes) + " in " + Utf8(discs) + "; " +
                     gigabytes(space.available) + " are free.";
    return result;
  }
  done = 0;
  total = bytes;
  started(label);
  const auto fail = [&](const std::string& text) {
    fs::remove_all(partial, error);
    result.message = text;
    return result;
  };
  std::string failure = UnpackDiscImage(image, first, partial, done, cancel);
  if (!failure.empty()) return fail(failure);
  const DiscCheck check = result.disc1 ? CheckDisc1(data, Utf8(partial)) : CheckDisc2(data, Utf8(partial));
  // The partial Disc 1 has only its executables yet: the release decides.
  if (result.disc1 ? check.release.empty() : !check.error.empty()) return fail(Utf8(image) + ": " + check.error);
  std::set<std::string> taken;
  for (const auto& entry : first) taken.insert(entry.path);
  std::vector<DiscImageEntry> rest;
  for (const auto& entry : read.entries)
    if (!taken.contains(entry.path)) rest.push_back(entry);
  failure = UnpackDiscImage(image, rest, partial, done, cancel);
  if (!failure.empty()) return fail(failure);
  fs::remove_all(folder, error);
  fs::rename(partial, folder, error);
  if (error) return fail("Unable to rename " + Utf8(partial) + ": " + error.message());
  std::ofstream(marker, std::ios::binary | std::ios::trunc)
      << Json{{"source", source}, {"files", files.size()}, {"bytes", bytes}}.dump(1);
  result.folder = Utf8(folder);
  result.message = label + (result.disc1 ? " (" + check.release + ")" : "") + ": " + std::to_string(files.size()) +
                   " files unpacked into " + result.folder + ". The ISO is not needed any more.";
  return result;
}

}  // namespace fm4
