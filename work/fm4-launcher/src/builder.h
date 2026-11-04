// The launcher's builder mode (docs/analysis/builder-installer.md 5 and 6, docs/running.md#builder-mode):
// the launcher of the builder package finds no built game beside it and builds one on this
// computer. It fetches the embeddable Python itself, then runs outputs/FM4-build/pipeline.py of
// the package's source snapshot for the other downloads and the build steps, and hands over to
// the launcher that the build writes.
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "plan.h"

namespace fm4 {

// The install root's folders (launcher.json builder.layout).
struct BuilderLayout {
  std::filesystem::path install, source, build, toolchain, downloads, logs;
  static BuilderLayout Of(const std::filesystem::path& install, const Json& layout);
  std::filesystem::path Pipeline() const { return source / "outputs" / "FM4-build" / "pipeline.py"; }
  std::filesystem::path State() const { return install / "builder-state.json"; }
  std::filesystem::path Backups() const { return install / "backups"; }
};

// Whether a folder is the install root of a builder package: launcher.json, and the source
// snapshot with its stamp and pipeline.py.
bool IsInstallRoot(const std::filesystem::path& directory);

// The snapshot's version stamp (source-stamp.json); empty fields when there is none.
struct SourceStamp {
  std::string commit, describe;
};
SourceStamp ReadStamp(const std::filesystem::path& source);

// The commit that build-result.json names (source.head), or empty.
std::string BuiltCommit(const std::filesystem::path& build);

// One download of launcher.json builder.downloads (builder_downloads in pipeline.py).
struct Download {
  std::string id, name, what, host, url, sha256, license, license_link, folder;
  std::uint64_t size = 0;
  bool exact = true;               // false: the size last measured, not pinned
  std::vector<std::string> files;  // what it leaves, below the install root
  Json marker;                     // a tool's install marker, or null
};
std::vector<Download> ReadDownloads(const Json& builder);
// The bytes of a download on disk: its files, or their .partial while they arrive.
std::uint64_t DownloadedBytes(const BuilderLayout& layout, const Download& item);
bool Downloaded(const BuilderLayout& layout, const Download& item);

// A --progress line of pipeline.py (docs/setup.md#progress-lines).
struct ProgressLine {
  std::string event, stage, step, state, log, error;
  double elapsed = 0;
};
bool ParseProgressLine(const std::string& text, ProgressLine& line);
// Ninja's last "[done/total]" in a log's text.
bool NinjaProgress(const std::string& text, int& done, int& total);

// A failure in plain words (the table of docs/analysis/builder-installer.md 6): what happened, what
// to do, and the wizard's screen that helps ("discs", "downloads" or empty).
struct PlainError {
  std::string text, action, screen;
};
PlainError Explain(const std::string& error, const std::string& step);

// The steps of launcher.json builder.steps: python, preflight, toolchain, setup, generate, build.
// Their names on the Build screen.
const char* BuilderStepTitle(const std::string& id);

enum class StepState { kWaiting, kRunning, kDone, kFailed, kPaused };
struct StepView {
  std::string id;
  StepState state = StepState::kWaiting;
  double seconds = 0;   // the time so far while running, the time taken when done
  double estimate = 0;  // launcher.json builder.steps
};

// What the pages show, copied under the builder's lock.
struct BuilderView {
  std::vector<StepView> steps;
  bool busy = false;
  std::string task;         // "downloads" or "build" while busy
  std::string downloading;  // the download being fetched
  std::string detail;       // the pipeline's running step (runtime-build, codegen, ...)
  std::string log;          // the running or failed step's log
  int ninja_done = 0, ninja_total = 0;
  std::string error;        // the last failure, as the pipeline or the launcher wrote it
  std::string failed_step;
  std::vector<std::string> preflight;  // the preflight's failed and warned checks, in words
  bool finished = false;    // every step is done: the launcher hands over
  std::string message;      // the last task's result
};

struct BuildInputs {
  std::string disc1, disc2;
  std::vector<std::string> content;
  // The player's files that an update backs up first: the user data folder and the launcher's
  // settings file (overlay-changes.json beside it too).
  std::filesystem::path user_data, settings;
};

// Updates (docs/analysis/builder-installer.md 5, B7). Puts the package at package into the
// install root install, which holds an earlier package and its build: the package's source is
// copied beside the install's as source.new, checked against its stamp and swapped in; the
// launcher, launcher.json, toolchain.json, the texts and the notices are copied over. build,
// toolchain, downloads, logs and backups stay; the next build of the install backs up the saves
// and rebuilds. Returns an error, or empty.
std::string UpdateInstall(const std::filesystem::path& package, const std::filesystem::path& install);
// Removes the files of a source snapshot that its stamp does not list (paths): what is left of an
// earlier package unpacked into the same folder. Returns how many were removed; none without the
// stamp's list (a package before B7).
size_t PruneSource(const std::filesystem::path& source);
// Copies the saves of user_data (each profile's folder and the achievements; not the imported
// content, which Disc 2 brings back, nor the cache) and the settings file with overlay-changes.json
// into folder. Returns an error, or empty.
std::string BackUpSaves(const BuildInputs& inputs, const std::filesystem::path& folder);
// Starts a launcher with arguments, in its folder. Returns an error, or empty.
std::string StartLauncher(const std::filesystem::path& program, const std::wstring& arguments = {});

class Builder {
 public:
  Builder(BuilderLayout layout, std::vector<Download> downloads, const Json& steps, SourceStamp stamp);
  ~Builder();
  // Fetches the downloads of ids: Python by the launcher itself, the others through pipeline.py's
  // download action, which needs Python first.
  void StartDownloads(std::vector<std::string> ids);
  // Runs each step that is not done yet, from the first. download_all: fetches the missing
  // downloads first (--build, the consent given on the command line).
  void StartBuild(const BuildInputs& inputs, bool download_all = false);
  // Ends the running task and every process it started; each step resumes from its marker.
  void Pause();
  BuilderView View();
  // --screenshot: shows a made-up view instead of the one on disk.
  void Show(const BuilderView& view);
  // Starts build\game-build\MotorsportDX.exe, the launcher of the build. Returns an error, or empty.
  std::string HandOver() const;
  // Whether the build of this source is done: game-build holds the game and the launcher, and
  // build-result.json names the stamp's commit.
  bool Built() const;
  // Copy details: the package, the step, the tools, the error and the logs, as text.
  std::string Details();
  const BuilderLayout& Layout() const { return layout_; }
  const std::vector<Download>& Downloads() const { return downloads_; }
  const SourceStamp& Stamp() const { return stamp_; }

 private:
  void Start(const char* task, std::function<void()> work);
  void Refresh();  // the steps' states from the markers and builder-state.json
  bool StepDone(const std::string& id, const Json& state, const BuildInputs* inputs) const;
  bool RunStep(size_t index, const BuildInputs& inputs);
  // Before the first build of a new package over an earlier build: the stale source files go and
  // the saves are backed up (once per package).
  bool PrepareUpdate(const BuildInputs& inputs);
  bool Fetch(const std::string& id);
  std::string FetchPython();
  // Runs args with the builder's environment, its output into log; returns the exit code, or -1
  // when paused. Reads the progress lines meanwhile.
  long Run(const std::vector<std::string>& args, const std::filesystem::path& log);
  void ReadProgress();
  void Fail(const std::string& step, const std::string& error);
  void Log(const std::string& text);
  Json LoadState() const;
  void SaveState(const Json& state) const;
  std::filesystem::path Python() const;
  std::filesystem::path ProgressFile() const { return layout_.logs / "progress.jsonl"; }

  BuilderLayout layout_;
  std::vector<Download> downloads_;
  SourceStamp stamp_;
  std::mutex mutex_;
  BuilderView view_;  // mutex_
  bool shown_ = false;
  std::thread thread_;
  std::atomic<bool> busy_{false}, cancel_{false};
  void* job_ = nullptr;  // mutex_: the running process's job object
  std::uintmax_t progress_offset_ = 0;
  std::chrono::steady_clock::time_point step_started_;
  int jobs_ = 4;
};

// Whether the computer runs on its battery now.
bool OnBattery();

}  // namespace fm4
