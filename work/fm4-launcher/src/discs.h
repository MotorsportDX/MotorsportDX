// Checks of the Discs and the content packages: ports of validate_discs, disc_release and
// validate_content in outputs/FM4-build/pipeline.py, with the tables of launcher.json.
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "plan.h"

namespace fm4 {

std::string Sha256File(const std::filesystem::path& path);

// disc2_packages of pipeline.py: the four content packages of Disc 2.
std::vector<std::filesystem::path> Disc2Packages(const std::string& disc2);

struct DiscCheck {
  std::string path;     // the resolved Disc root; empty when none was given
  std::string release;  // Disc 1: the release of its executables
  std::string error;    // empty when the Disc is usable
};

DiscCheck CheckDisc1(const LauncherData& data, const std::string& disc1);
DiscCheck CheckDisc2(const LauncherData& data, const std::string& disc2);

struct Package {
  std::filesystem::path path;
  std::uintmax_t size = 0;
};

struct ContentCheck {
  std::vector<Package> packages;     // Disc 2's four, then those of each content path
  std::vector<std::string> skipped;  // files of a content directory that are not FM4 content
  std::string error;                 // empty when the content is usable
};

ContentCheck CheckContent(const LauncherData& data, const std::string& disc2,
                          const std::vector<std::string>& content);

struct DiscUnpack {
  bool disc1 = false;   // the image is Disc 1, else Disc 2
  std::string folder;   // the unpacked Disc; empty when it failed
  std::string message;  // the result or the error
};

// disc_input of pipeline.py for the Game data page: an ISO is Disc 1 or Disc 2 by its files and
// is unpacked into <discs>/disc1 or <discs>/disc2. The files that identify it are unpacked and
// checked first, so a wrong image fails in seconds; <discs>/<name>.json records the image, so the
// same image is not unpacked again. done and total count the bytes; started gets "Disc 1" or
// "Disc 2" when the unpacking starts.
DiscUnpack UnpackDisc(const LauncherData& data, const std::filesystem::path& discs, const std::filesystem::path& image,
                      std::atomic<std::uint64_t>& done, std::atomic<std::uint64_t>& total,
                      const std::atomic<bool>& cancel, const std::function<void(const std::string&)>& started);

}  // namespace fm4
