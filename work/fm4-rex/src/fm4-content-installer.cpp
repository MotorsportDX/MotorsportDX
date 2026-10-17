#include <rex/runtime.h>
#include <rex/filesystem.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xam/content_manager.h>
#include <rex/filesystem/devices/stfs_container_device.h>
#include <rex/filesystem/devices/host_path_device.h>
#include <rex/filesystem/vfs.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>

void FM4MountCache(rex::Runtime* runtime) {
  // FM4's replay stream constructor expects cache:\\ to be writable. Its caller
  // dereferences the returned stream, so the SDK default of no cache device
  // fails here (guest 82930190 after the cache-root open fails).
  const auto directory = runtime->cache_root() / "fm4-game-cache";
  std::filesystem::create_directories(directory);
  constexpr auto device_path = "\\Device\\FM4Cache";
  auto device = std::make_unique<rex::filesystem::HostPathDevice>(
      device_path, directory, false, true);
  auto* vfs = runtime->file_system();
  if (!device->Initialize() || !vfs->RegisterDevice(std::move(device)) ||
      !vfs->RegisterSymbolicLink("cache:", device_path))
    throw std::runtime_error("Unable to mount FM4 game cache");
  std::fprintf(stderr, "FM4 cache: mounted %s\n", directory.string().c_str());
}

// Import of the user's content packages: Disc 2 and downloadable content. The launcher
// validates them and lists one UTF-8 path a line in FM4_CONTENT_MANIFEST
// (pipeline.py write_content_manifest). The runtime's installer preserves the package
// metadata and extracts each package into user_data_root under its file name; a marker
// next to it records the package size, so an unchanged package is imported once.
void FM4InstallContent(rex::Runtime* runtime) {
  const char* manifest_path = std::getenv("FM4_CONTENT_MANIFEST");
  if (!manifest_path || !*manifest_path) return;
  auto* kernel = runtime->kernel_state();
  if (kernel->title_id() != 0x4D530910)
    throw std::runtime_error("Content import requires FM4's loaded title ID");
  std::ifstream manifest(rex::to_path(std::string_view(manifest_path)));
  if (!manifest) throw std::runtime_error(std::string("Unable to read ") + manifest_path);
  auto* content = kernel->content_manager();
  size_t count = 0;
  for (std::string line; std::getline(manifest, line);) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) continue;
    ++count;
    const auto package = rex::to_path(std::string_view(line));
    const auto name = rex::path_to_utf8(package.filename());
    const auto header = rex::filesystem::StfsContainerDevice::ReadPackageHeader(package);
    if (!header || uint32_t(header->metadata.execution_info.title_id) != 0x4D530910 ||
        header->metadata.content_type != rex::filesystem::XContentType::kMarketplaceContent)
      throw std::runtime_error("Invalid FM4 content package metadata: " + line);
    const auto marker = runtime->user_data_root() / (package.filename().native() + L".import-complete");
    const auto size = std::filesystem::file_size(package);
    uintmax_t previous_size = 0;
    { std::ifstream previous(marker); previous >> previous_size; }
    const auto destination = runtime->user_data_root() / "0000000000000000" /
        "4D530910" / "00000002" / package.filename();
    if (previous_size == size && std::filesystem::is_directory(destination)) {
      std::fprintf(stderr, "FM4 content: %s already imported\n", name.c_str());
      continue;
    }
    std::fprintf(stderr, "FM4 content: importing %s (%llu bytes)\n", name.c_str(),
                 static_cast<unsigned long long>(size));
    std::fflush(stderr);
    const auto result = content->InstallContent(package);
    if (result != 0)
      throw std::runtime_error("Content import of " + name + " failed: " + std::to_string(result));
    size_t files = 0;
    uintmax_t bytes = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(destination)) {
      if (entry.is_regular_file()) { ++files; bytes += entry.file_size(); }
    }
    if (!files || !bytes) throw std::runtime_error("Content import of " + name + " produced no files");
    std::ofstream record(marker);
    record << size << '\n' << files << '\n' << bytes << '\n';
    record.flush();
    if (!record) throw std::runtime_error("Unable to record the import of " + name);
    std::fprintf(stderr, "FM4 content: imported %s files=%zu bytes=%llu\n", name.c_str(), files,
                 static_cast<unsigned long long>(bytes));
    std::fflush(stderr);
  }
  // Developer import mode stops before the guest entry point is launched.
  if (std::getenv("FM4_CONTENT_INSTALL_ONLY")) {
    std::fprintf(stderr, "FM4 content: %zu content packages installed\n", count);
    std::fflush(stderr);
    std::exit(0);
  }
}
