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

// mods.CHAIN_FILES: the files whose hashes chain stacked mods (built_against key, disc path).
const std::pair<const char*, const char*> kChainFiles[] = {{"fm4_gamedb_sha256", "Media/DB/gamedb.slt"},
                                                           {"fm4_zipmanifest_sha256", "Media/zipmanifest.xml"}};
using DiscState = std::map<std::string, std::string>;

// mods.disc_state: the hashes of Disc 1's files a mod built on it records.
DiscState StateOf(const fs::path& disc1) {
  DiscState state;
  for (const auto& [key, relative] : kChainFiles) state[key] = Sha256File(disc1 / PathFromUtf8(relative));
  return state;
}

// mods.after: the hashes once mod is installed over the disc of state.
DiscState After(const Json& mod, DiscState state) {
  for (const auto& [key, relative] : kChainFiles)
    for (const auto& [path, entry] : mod.at("files").items())
      if (Lower(path) == Lower(relative)) state[key] = entry.at("sha256").get<std::string>();
  return state;
}

bool Fits(const Json& mod, const DiscState& state) {
  const Json& built = mod.at("built_against");
  for (const auto& [key, relative] : kChainFiles)
    if (built.at(key).get<std::string>() != state.at(key)) return false;
  return true;
}

std::vector<std::string> BaseIds(const Json& mod) {
  std::vector<std::string> ids;
  if (mod.contains("base_packages") && mod.at("base_packages").is_array())
    for (const auto& base : mod.at("base_packages")) ids.push_back(base.at("id").get<std::string>());
  return ids;
}

bool SamePacks(const fs::path& root, const Json& mod) {
  std::map<std::string, std::uint64_t> wanted;
  for (const auto& pack : mod.at("built_against").at("content_packs"))
    wanted[Lower(pack.at("path").get<std::string>())] = pack.at("size").get<std::uint64_t>();
  return wanted == ContentPacks(root);
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
      tools.error = "Mods are converted by the build tools of a Motorsport DX built with the builder "
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

std::string OrderMods(const fs::path& root, const std::string& disc1_text, const std::vector<std::string>& ids,
                      std::vector<std::string>& ordered) {
  ordered.clear();
  try {
    const fs::path disc1 = PathFromUtf8(disc1_text);
    std::map<std::string, Json> remaining;
    for (const auto& id : ids) {
      if (remaining.contains(id)) continue;
      const fs::path folder = root / kMods / PathFromUtf8(id);
      std::error_code error;
      if (!fs::is_regular_file(folder / "mod.json", error)) return "There is no mod " + id + ": convert it first.";
      remaining[id] = LoadMod(folder);
    }
    const DiscState start = StateOf(disc1);
    DiscState state = start;
    const auto created = [&](const std::string& id) { return remaining.at(id).value("created", ""); };
    while (!remaining.empty()) {
      std::vector<std::string> ready;
      for (const auto& [id, mod] : remaining)
        if (Fits(mod, state)) ready.push_back(id);
      std::stable_sort(ready.begin(), ready.end(), [&](const auto& a, const auto& b) { return created(a) < created(b); });
      if (ready.size() > 1)
        return "The mods " + ready[0] + " and " + ready[1] + " were each converted without the other: convert " +
               ready[1] + " again, with " + ready[0] + " converted.";
      if (ready.empty()) {
        std::string id = remaining.begin()->first;
        for (const auto& [other, mod] : remaining)
          if (created(other) < created(id)) id = other;
        std::vector<std::string> missing;
        for (const auto& base : BaseIds(remaining.at(id)))
          if (!remaining.contains(base) && std::find(ordered.begin(), ordered.end(), base) == ordered.end())
            missing.push_back(base);
        if (!missing.empty()) {
          std::string names;
          for (const auto& base : missing) names += (names.empty() ? "" : ", ") + base;
          return "The mod " + id + " was converted with " + names + ": play them together.";
        }
        if (!ordered.empty() && Fits(remaining.at(id), start))
          return "The mods " + ordered.front() + " and " + id + " were each converted without the other: convert " +
                 id + " again, with " + ordered.front() + " converted.";
        return "The mod " + id + " was converted for another Disc 1 or with other mods: convert it again.";
      }
      const std::string id = ready.front();
      if (!SamePacks(root, remaining.at(id)))
        return "The mod " + id + " was converted with other content packages than those imported now: convert it again.";
      state = After(remaining.at(id), state);
      remaining.erase(id);
      ordered.push_back(id);
    }
    return {};
  } catch (const std::exception& exception) {
    return exception.what();
  }
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
      info.content = mod.value("kind", "") == "track-overlay" ? "tracks" : "cars";
      for (const auto& item : mod.contains(info.content) ? mod.at(info.content) : mod.at("cars")) {
        info.skipped += item.value("status", "") == "skipped";
        if (item.value("status", "") == "packaged" && item.contains("layouts") && item.at("layouts").is_array())
          info.layouts += static_cast<int>(item.at("layouts").size());
      }
      info.base = BaseIds(mod);
      std::vector<std::string> chain = info.base, ordered;
      chain.push_back(info.id);
      info.error = disc1.empty() ? "Choose Disc 1 first." : OrderMods(root, disc1, chain, ordered);
    } catch (const std::exception& exception) {
      info.error = exception.what();
    }
    mods.push_back(info);
  }
  std::sort(mods.begin(), mods.end(), [](const ModInfo& a, const ModInfo& b) {
    return a.base.size() != b.base.size() ? a.base.size() < b.base.size() : a.id < b.id;
  });
  return mods;
}

fs::path ModDisc(const fs::path& root) { return root / kModDisc; }

bool ModDiscReady(const fs::path& root, const std::string& disc1, const std::vector<std::string>& ids) {
  try {
    std::ifstream stream(root / kModDisc / kMarker, std::ios::binary);
    if (!stream) return false;
    const Json marker = Json::parse(stream);
    const Json& mods = marker.at("mods");
    const std::set<std::string> wanted(ids.begin(), ids.end());
    if (!SamePath(marker.at("disc1").get<std::string>(), disc1) || mods.size() != wanted.size()) return false;
    for (const auto& id : wanted)
      if (!mods.contains(id) ||
          mods.at(id).get<std::string>() != Sha256File(root / kMods / PathFromUtf8(id) / "mod.json"))
        return false;
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

std::string PrepareModDisc(const fs::path& root, const std::string& disc1_text, const std::vector<std::string>& ids,
                           std::atomic<std::uint64_t>& done, std::atomic<std::uint64_t>& total,
                           const std::atomic<bool>& cancel) {
  try {
    const fs::path disc1 = PathFromUtf8(disc1_text);
    std::vector<std::string> ordered;
    if (const std::string error = OrderMods(root, disc1_text, ids, ordered); !error.empty()) return error;
    // lower-case path: (the mod's files folder, the mod's spelling); a later mod's file wins.
    std::map<std::string, std::pair<fs::path, std::string>> writes;
    Json marker_mods = Json::object();
    for (const auto& id : ordered) {
      const fs::path folder = root / kMods / PathFromUtf8(id);
      const Json mod = LoadMod(folder);
      const fs::path files = folder / PathFromUtf8(mod.at("files_root").get<std::string>());
      // mods.verify: every listed file present, of its size and SHA-256, and nothing else.
      std::set<std::string> listed;
      for (const auto& [relative, entry] : mod.at("files").items()) {
        if (relative.find("..") != std::string::npos || relative.find(':') != std::string::npos || relative.front() == '/')
          return "The mod " + id + " lists a refused path: " + relative + ".";
        const fs::path path = files / PathFromUtf8(relative);
        std::error_code error;
        if (!fs::is_regular_file(path, error) || fs::file_size(path, error) != entry.at("size").get<std::uint64_t>() ||
            Sha256File(path) != entry.at("sha256").get<std::string>())
          return relative + " of the mod " + id + " is missing or changed: convert it again.";
        listed.insert(Lower(relative));
      }
      for (const auto& file : fs::recursive_directory_iterator(files))
        if (file.is_regular_file() && !listed.contains(Lower(Utf8(fs::relative(file.path(), files).generic_wstring()))))
          return Utf8(file.path()) + " is not listed in the mod.json of " + id + ": convert it again.";
      for (const auto& relative : mod.at("writes")) {
        const std::string text = relative.get<std::string>();
        writes[Lower(text)] = {files, text};
      }
      marker_mods[id] = Sha256File(folder / "mod.json");
    }
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
    // A new file for each the mods write: never written through a link to Disc 1.
    for (const auto& [lower, source] : writes) {
      const auto& [files, relative] = source;
      const fs::path destination = stage / PathFromUtf8(relative);
      fs::create_directories(destination.parent_path());
      fs::copy_file(files / PathFromUtf8(relative), destination);
      ++done;
    }
    Json marker = Json::object();
    marker["disc1"] = Utf8(disc1);
    marker["mods"] = marker_mods;
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

void ModRunner::Start(const ModTools& tools, const fs::path& root, const std::string& game,
                      const std::string& content, const std::string& disc, const std::string& disc2) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (view_.busy) return;
    view_ = ModRunView{};
    view_.busy = true;
    view_.game = game;
    view_.content = content;
    view_.log = root / "logs" / "mods-run.log";
  }
  if (thread_.joinable()) thread_.join();
  stopped_ = false;
  std::vector<std::string> args = {Utf8(tools.python), "-I", Utf8(tools.pipeline), "mods", "--build-root", Utf8(root),
                                   "--toolchain", Utf8(tools.toolchain), "--mod-game", game,
                                   "--mod-content", content, "--mod-disc", disc};
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
