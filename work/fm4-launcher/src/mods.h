// Car mods on the launcher's Mods page (docs/running.md#car-mods): the mods action of
// outputs/FM4-build/mods.py run with the builder install's Python, the mods in BuildRoot\mods, and
// the mod Disc that Play reads. PrepareModDisc ports mods.mod_disc: the same checks and the same
// marker, so a mod Disc either side prepared serves the other.
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "plan.h"

namespace fm4 {

// The pipeline that converts: pipeline.py of a source snapshot, the Python that runs it and the
// portable toolchain (llvm-mingw's clang builds the LZX decoder). The builder install has all
// three; FM4_MODS_SOURCE (a checkout), FM4_MODS_PYTHON and FM4_MODS_TOOLCHAIN name them
// elsewhere. error says what is missing.
struct ModTools {
  std::filesystem::path pipeline, python, toolchain;
  std::string error;
};
ModTools FindModTools(const std::filesystem::path& root, const LauncherData& data);

struct ModInfo {
  std::string id, name, source_game, created;
  int packaged = 0, failed = 0, skipped = 0;
  std::string error;  // why it cannot be played on this Disc 1 and these packages; empty when it can
};
// The mods of root\mods, each checked against disc1 and the imported content packages.
std::vector<ModInfo> ListMods(const std::filesystem::path& root, const std::string& disc1);

// The folder Play reads instead of Disc 1 when a mod is played.
std::filesystem::path ModDisc(const std::filesystem::path& root);
// Whether root\mod-disc holds Disc 1 with the mod id over it (its marker names both).
bool ModDiscReady(const std::filesystem::path& root, const std::string& disc1, const std::string& id);
// Builds root\mod-disc: Disc 1's files hard-linked (copied across volumes), the mod's files
// copied. done and total count the files. Returns an error, or empty.
std::string PrepareModDisc(const std::filesystem::path& root, const std::string& disc1, const std::string& id,
                           std::atomic<std::uint64_t>& done, std::atomic<std::uint64_t>& total,
                           const std::atomic<bool>& cancel);

// What the Mods page shows of a running conversion, from its log.
struct ModRunView {
  bool busy = false;
  std::string game;         // fm3 or fm2
  std::string stage;        // the last "Mods: ..." line
  int index = 0, total = 0;  // the last "[index/total] car: status" line
  int failed = 0;
  std::string last_car;
  std::string message;      // the result, or the error
  std::filesystem::path log;
};

// Runs pipeline.py mods in a job object, so Stop and the launcher's exit end every process.
class ModRunner {
 public:
  ~ModRunner();
  void Start(const ModTools& tools, const std::filesystem::path& root, const std::string& game,
             const std::string& disc, const std::string& disc2);
  void Stop();
  ModRunView View();

 private:
  void Read();
  std::mutex mutex_;
  ModRunView view_;
  std::thread thread_;
  void* job_ = nullptr;  // mutex_
  std::atomic<bool> stopped_{false};
};

}  // namespace fm4
