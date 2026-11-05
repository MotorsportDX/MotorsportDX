#include "mods.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

#include "builder.h"
#include "discs.h"
#include "runner.h"

namespace fm4 {

namespace fs = std::filesystem;

namespace {

// mods.py's names (outputs/FM4-build/mods.py).
const char* const kMods = "mods";
const char* const kModDisc = "mod-disc";
const char* const kMarker = ".fm4-mod-disc.json";
const fs::path kContentRoot = fs::path(L"user-data") / L"0000000000000000" / L"4D530910" / L"00000002";

std::string Lower(std::string text) {
  for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return text;
}

std::string Environment(const char* name) {
  wchar_t buffer[32768];
  const DWORD size = GetEnvironmentVariableW(std::wstring(name, name + std::strlen(name)).c_str(), buffer, 32768);
  return size > 0 && size < 32768 ? Utf8(fs::path(std::wstring(buffer, size))) : std::string();
}

std::wstring Wide(const std::string& text) { return PathFromUtf8(text).wstring(); }

std::string ReadFile(const fs::path& path) {
  std::ifstream stream(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

Json LoadMod(const fs::path& folder) {
  std::ifstream stream(folder / "mod.json", std::ios::binary);
  Json mod = Json::parse(stream);
  if (mod.value("format", "") != "fm4-mod" || mod.value("format_version", 0) != 1)
    throw std::runtime_error(Utf8(folder) + " is not an fm4-mod package of version 1.");
  return mod;
}

// mods.content_packs: the imported content packages as built_against lists them, by lower-case path.
std::map<std::string, std::uint64_t> ContentPacks(const fs::path& root) {
  std::map<std::string, std::uint64_t> packs;
  const fs::path content = root / kContentRoot;
  std::error_code error;
  if (!fs::is_directory(content, error)) return packs;
  for (const auto& package : fs::directory_iterator(content, error)) {
    const fs::path zips = package.path() / "Media" / "DLCZips";
    if (!fs::is_directory(zips, error)) continue;
    for (const auto& file : fs::directory_iterator(zips, error))
      if (file.is_regular_file() && Lower(file.path().extension().string()) == ".zip")
        packs[Lower(Utf8(fs::relative(file.path(), content).generic_wstring()))] = file.file_size();
  }
  return packs;
}

// mods.mod_error: why the mod cannot be played on this Disc 1 and these content packages.
std::string ModError(const fs::path& root, const Json& mod, const fs::path& disc1) {
  const Json& built = mod.at("built_against");
  const fs::path media = disc1 / "Media";
  if (Sha256File(media / "DB" / "gamedb.slt") != built.at("fm4_gamedb_sha256").get<std::string>() ||
      Sha256File(media / "zipmanifest.xml") != built.at("fm4_zipmanifest_sha256").get<std::string>())
    return "It was built for another Disc 1: convert it again.";
  std::map<std::string, std::uint64_t> wanted;
  for (const auto& pack : built.at("content_packs"))
    wanted[Lower(pack.at("path").get<std::string>())] = pack.at("size").get<std::uint64_t>();
  if (wanted != ContentPacks(root))
    return "It was built with other content packages than those imported now: convert it again.";
  return {};
}

bool SamePath(const std::string& a, const std::string& b) {
  std::error_code error;
  return fs::equivalent(PathFromUtf8(a), PathFromUtf8(b), error) && !error;
}

}  // namespace

ModTools FindModTools(const fs::path& root, const LauncherData& data) {
  ModTools tools;
  const std::string source = Environment("FM4_MODS_SOURCE");
  if (!source.empty()) {
    tools.pipeline = PathFromUtf8(source) / "outputs" / "FM4-build" / "pipeline.py";
    tools.python = PathFromUtf8(Environment("FM4_MODS_PYTHON"));
    tools.toolchain = PathFromUtf8(Environment("FM4_MODS_TOOLCHAIN"));
  } else {
    const fs::path install = root.parent_path();
    if (!IsInstallRoot(install) || !data.json.contains("builder")) {
      tools.error = "Car mods are converted by the build tools of a Motorsport DX built with the builder "
                    "package; this game has none beside it. Use Build.ps1 -Action mods.";
      return tools;
    }
    const Json& builder = data.json.at("builder");
    const BuilderLayout layout = BuilderLayout::Of(install, builder.at("layout"));
    tools.pipeline = layout.Pipeline();
    tools.toolchain = layout.toolchain;
    for (const auto& item : ReadDownloads(builder))
      if (item.id == "python") tools.python = (install / PathFromUtf8(item.folder)).make_preferred() / "python.exe";
  }
  std::error_code error;
  if (!fs::is_regular_file(tools.pipeline, error))
    tools.error = "The build's pipeline.py is missing: " + Utf8(tools.pipeline) + ".";
  else if (!fs::is_regular_file(tools.python, error))
    tools.error = "The build's Python is missing: " + Utf8(tools.python) + ".";
  else if (!fs::is_regular_file(tools.toolchain / "toolchain-state.json", error))
    tools.error = "The build tools are missing: " + Utf8(tools.toolchain) + ".";
  return tools;
}

std::vector<ModInfo> ListMods(const fs::path& root, const std::string& disc1) {
  std::vector<ModInfo> mods;
  std::error_code error;
  if (!fs::is_directory(root / kMods, error)) return mods;
  for (const auto& entry : fs::directory_iterator(root / kMods, error)) {
    if (!fs::is_regular_file(entry.path() / "mod.json", error)) continue;
    ModInfo info;
    info.id = Utf8(entry.path().filename());
    try {
      const Json mod = LoadMod(entry.path());
      info.name = mod.value("name", info.id);
      info.source_game = mod.value("source_game", "");
      info.created = mod.value("created", "");
      info.packaged = mod.value("packaged", 0);
      info.failed = mod.value("failed", 0);
      for (const auto& car : mod.at("cars")) info.skipped += car.value("status", "") == "skipped";
      info.error = disc1.empty() ? "Choose Disc 1 first." : ModError(root, mod, PathFromUtf8(disc1));
    } catch (const std::exception& exception) {
      info.error = exception.what();
    }
    mods.push_back(info);
  }
  std::sort(mods.begin(), mods.end(), [](const ModInfo& a, const ModInfo& b) { return a.id < b.id; });
  return mods;
}

fs::path ModDisc(const fs::path& root) { return root / kModDisc; }

bool ModDiscReady(const fs::path& root, const std::string& disc1, const std::string& id) {
  try {
    std::ifstream stream(root / kModDisc / kMarker, std::ios::binary);
    if (!stream) return false;
    const Json marker = Json::parse(stream);
    const Json& mods = marker.at("mods");
    return SamePath(marker.at("disc1").get<std::string>(), disc1) && mods.size() == 1 && mods.contains(id) &&
           mods.at(id).get<std::string>() == Sha256File(root / kMods / PathFromUtf8(id) / "mod.json");
  } catch (const std::exception&) {
    return false;
  }
}

std::string PrepareModDisc(const fs::path& root, const std::string& disc1_text, const std::string& id,
                           std::atomic<std::uint64_t>& done, std::atomic<std::uint64_t>& total,
                           const std::atomic<bool>& cancel) {
  try {
    const fs::path disc1 = PathFromUtf8(disc1_text), folder = root / kMods / PathFromUtf8(id);
    const Json mod = LoadMod(folder);
    if (const std::string error = ModError(root, mod, disc1); !error.empty()) return error;
    const fs::path files = folder / PathFromUtf8(mod.at("files_root").get<std::string>());
    // mods.verify: every listed file present, of its size and SHA-256, and nothing else.
    std::map<std::string, std::string> writes;  // lower-case path: the mod's spelling
    for (const auto& [relative, entry] : mod.at("files").items()) {
      if (relative.find("..") != std::string::npos || relative.find(':') != std::string::npos || relative.front() == '/')
        return "The mod lists a refused path: " + relative + ".";
      const fs::path path = files / PathFromUtf8(relative);
      std::error_code error;
      if (!fs::is_regular_file(path, error) || fs::file_size(path, error) != entry.at("size").get<std::uint64_t>() ||
          Sha256File(path) != entry.at("sha256").get<std::string>())
        return relative + " of the mod is missing or changed: convert it again.";
      writes[Lower(relative)] = relative;
    }
    for (const auto& file : fs::recursive_directory_iterator(files))
      if (file.is_regular_file() && !writes.contains(Lower(Utf8(fs::relative(file.path(), files).generic_wstring()))))
        return Utf8(file.path()) + " is not listed in mod.json: convert it again.";
    std::vector<fs::path> disc_files;
    for (const auto& file : fs::recursive_directory_iterator(disc1))
      if (file.is_regular_file()) disc_files.push_back(file.path());
    total = disc_files.size() + writes.size();
    done = 0;
    const fs::path target = root / kModDisc, stage = fs::path(target).concat(L".stage");
    std::error_code ignored;
    fs::remove_all(stage, ignored);
    for (const auto& path : disc_files) {
      if (cancel) return "Stopped.";
      const fs::path relative = fs::relative(path, disc1);
      if (!writes.contains(Lower(Utf8(relative.generic_wstring())))) {
        const fs::path destination = stage / relative;
        fs::create_directories(destination.parent_path());
        std::error_code link;
        fs::create_hard_link(path, destination, link);
        if (link) fs::copy_file(path, destination);
      }
      ++done;
    }
    // A new file for each the mod writes: never written through a link to Disc 1.
    for (const auto& [lower, relative] : writes) {
      const fs::path destination = stage / PathFromUtf8(relative);
      fs::create_directories(destination.parent_path());
      fs::copy_file(files / PathFromUtf8(relative), destination);
      ++done;
    }
    Json marker = Json::object();
    marker["disc1"] = Utf8(disc1);
    marker["mods"] = Json::object();
    marker["mods"][id] = Sha256File(folder / "mod.json");
    std::ofstream(stage / kMarker, std::ios::binary) << marker.dump(1);
    fs::remove_all(target);
    fs::rename(stage, target);
    return {};
  } catch (const std::exception& exception) {
    return exception.what();
  }
}

ModRunner::~ModRunner() {
  Stop();
  if (thread_.joinable()) thread_.join();
}

void ModRunner::Start(const ModTools& tools, const fs::path& root, const std::string& game, const std::string& disc,
                      const std::string& disc2) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (view_.busy) return;
    view_ = ModRunView{};
    view_.busy = true;
    view_.game = game;
    view_.log = root / "logs" / "mods-run.log";
  }
  if (thread_.joinable()) thread_.join();
  stopped_ = false;
  std::vector<std::string> args = {Utf8(tools.python), "-I", Utf8(tools.pipeline), "mods", "--build-root", Utf8(root),
                                   "--toolchain", Utf8(tools.toolchain), "--mod-game", game, "--mod-disc", disc};
  if (!disc2.empty()) args.insert(args.end(), {"--mod-disc2", disc2});
  thread_ = std::thread([this, args, root] {
    const fs::path log = root / "logs" / "mods-run.log";
    std::string message;
    try {
      fs::create_directories(log.parent_path());
      SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
      HANDLE output = CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit,
                                  CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
      if (output == INVALID_HANDLE_VALUE) throw std::runtime_error("Unable to write " + Utf8(log));
      // A job object: Stop and the launcher's exit end the pipeline with the converter it started.
      HANDLE job = CreateJobObjectW(nullptr, nullptr);
      JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
      limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
      SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
      STARTUPINFOW startup{};
      startup.cb = sizeof(startup);
      startup.dwFlags = STARTF_USESTDHANDLES;
      startup.hStdOutput = output;
      startup.hStdError = output;
      PROCESS_INFORMATION info{};
      std::wstring command = Wide(CommandLine(args)), environment = ChildEnvironment();
      const BOOL started = CreateProcessW(Wide(args.front()).c_str(), command.data(), nullptr, nullptr, TRUE,
                                          CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED | CREATE_NO_WINDOW,
                                          environment.data(), root.c_str(), &startup, &info);
      const DWORD start_error = GetLastError();
      CloseHandle(output);
      if (!started) {
        CloseHandle(job);
        throw std::runtime_error("Unable to start " + args.front() + " (Windows error " +
                                 std::to_string(start_error) + ")");
      }
      AssignProcessToJobObject(job, info.hProcess);
      {
        std::lock_guard<std::mutex> lock(mutex_);
        job_ = job;
      }
      if (stopped_) TerminateJobObject(job, 1);
      ResumeThread(info.hThread);
      CloseHandle(info.hThread);
      while (WaitForSingleObject(info.hProcess, 250) == WAIT_TIMEOUT) Read();
      Read();
      DWORD exit = 0;
      GetExitCodeProcess(info.hProcess, &exit);
      CloseHandle(info.hProcess);
      {
        std::lock_guard<std::mutex> lock(mutex_);
        job_ = nullptr;
      }
      CloseHandle(job);
      // The pipeline's last words: "Mod fm3-cars: 81 cars converted, ..." or "ERROR: ...".
      std::istringstream lines(ReadFile(log));
      std::string last;
      for (std::string line; std::getline(lines, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.starts_with("Mod ") || line.starts_with("ERROR: ")) last = line;
      }
      message = stopped_ ? "Stopped. A new conversion reuses what was fetched and extracted."
                : exit == 0 && !last.empty() ? last
                : !last.empty()              ? last
                                             : "The conversion stopped (exit " + std::to_string(exit) + ").";
    } catch (const std::exception& exception) {
      message = exception.what();
    }
    std::lock_guard<std::mutex> lock(mutex_);
    view_.message = message;
    view_.busy = false;
  });
}

void ModRunner::Stop() {
  stopped_ = true;
  std::lock_guard<std::mutex> lock(mutex_);
  if (job_) TerminateJobObject(job_, 1);
}

ModRunView ModRunner::View() {
  std::lock_guard<std::mutex> lock(mutex_);
  return view_;
}

// The pipeline's output (mods.convert): "Mods: <stage>" and "  [index/total] car: status (error)".
void ModRunner::Read() {
  fs::path log;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    log = view_.log;
  }
  std::istringstream lines(ReadFile(log));
  ModRunView parsed;
  for (std::string line; std::getline(lines, line);) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.starts_with("Mods: ")) parsed.stage = line.substr(6);
    const size_t open = line.find('['), slash = line.find('/', open), close = line.find("] ", open);
    if (line.starts_with("  [") && open != std::string::npos && slash < close && close != std::string::npos) {
      try {
        parsed.index = std::stoi(line.substr(open + 1, slash - open - 1));
        parsed.total = std::stoi(line.substr(slash + 1, close - slash - 1));
      } catch (const std::exception&) {
        continue;
      }
      const std::string rest = line.substr(close + 2);
      parsed.last_car = rest.substr(0, rest.find(':'));
      parsed.failed += rest.find(": failed") != std::string::npos;
    }
  }
  std::lock_guard<std::mutex> lock(mutex_);
  view_.stage = parsed.stage;
  view_.index = parsed.index;
  view_.total = parsed.total;
  view_.failed = parsed.failed;
  view_.last_car = parsed.last_car;
}

}  // namespace fm4
