// Checks of the Discs and the content packages: ports of validate_discs, disc_release and
// validate_content in outputs/FM4-build/pipeline.py, with the tables of launcher.json.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "plan.h"

namespace fm4 {

std::string Sha256File(const std::filesystem::path& path);

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

}  // namespace fm4
