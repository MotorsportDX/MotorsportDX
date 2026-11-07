// Mods on the launcher's Mods page (docs/running-mods.md): the mods action of
// outputs/FM4-build/mods.py run with the builder install's Python, the mods in BuildRoot\mods, and
// the mod Disc that Play reads. Mods stack like DLC packs: each was built on the disc that Disc 1
// and the mods under it make, and several play together. PrepareModDisc ports mods.mod_disc (and
// mods.order): the same checks and the same marker, so a mod Disc either side prepared serves the
// other.
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
  std::string content;            // cars or tracks
  std::vector<std::string> base;  // the mods it was built on (installed under it), in install order
  int packaged = 0, failed = 0, skipped = 0;
  int layouts = 0;    // tracks: the layouts of the packaged environments
  std::string error;  // why it cannot be played (with base) on this Disc 1 and these packages; empty when it can
};
// The mods of root\mods, each checked with the mods it was built on against disc1 and the
// imported content packages; a mod after the mods it was built on.
std::vector<ModInfo> ListMods(const std::filesystem::path& root, const std::string& disc1);

// mods.order: ids in install order (each built on the disc that Disc 1 and the mods before it
// make), or an error naming the mod that does not fit.
std::string OrderMods(const std::filesystem::path& root, const std::string& disc1, const std::vector<std::string>& ids,
                      std::vector<std::string>& ordered);

// The folder Play reads instead of Disc 1 when mods are played.
std::filesystem::path ModDisc(const std::filesystem::path& root);
// Whether root\mod-disc holds Disc 1 with the mods of ids over it (its marker names them all).
bool ModDiscReady(const std::filesystem::path& root, const std::string& disc1, const std::vector<std::string>& ids);
// Builds root\mod-disc: Disc 1's files hard-linked (copied across volumes), the mods' files copied
// in install order, a file several write from the last. done and total count the files. Returns an
// error, or empty.
std::string PrepareModDisc(const std::filesystem::path& root, const std::string& disc1, const std::vector<std::string>& ids,
                           std::atomic<std::uint64_t>& done, std::atomic<std::uint64_t>& total,
                           const std::atomic<bool>& cancel);

// What the Mods page shows of a running conversion, from its log.
struct ModRunView {
  bool busy = false;
  std::string game;         // fm3 or fm2
  std::string content;      // cars or tracks
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
             const std::string& content, const std::string& disc, const std::string& disc2);
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
