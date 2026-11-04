#include "builder.h"

#include <windows.h>

#include <algorithm>
#include <ctime>
#include <fstream>
#include <sstream>

#include <set>

#include "discs.h"
#include "overlay.h"
#include "runner.h"
#include "shaders.h"

namespace fm4 {

namespace fs = std::filesystem;

namespace {

std::wstring Wide(const std::string& text) {
  if (text.empty()) return {};
  const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
  std::wstring wide(static_cast<size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), size);
  return wide;
}

Json ReadJson(const fs::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) return nullptr;
  try {
    return Json::parse(stream);
  } catch (const std::exception&) {
    return nullptr;
  }
}

std::string ReadText(const fs::path& path, std::uintmax_t tail = 0) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) return {};
  if (tail) {
    stream.seekg(0, std::ios::end);
    const std::streamoff size = stream.tellg();
    stream.seekg(std::max<std::streamoff>(0, size - static_cast<std::streamoff>(tail)));
  }
  return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

std::string Environment(const char* name) {
  char value[4096];
  const DWORD length = GetEnvironmentVariableA(name, value, sizeof(value));
  return length && length < sizeof(value) ? std::string(value, length) : std::string();
}

std::string Now() {
  const std::time_t now = std::time(nullptr);
  std::tm local{};
  localtime_s(&local, &now);
  char text[32];
  std::strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S", &local);
  return text;
}

// The variables the builder's processes keep of the launcher's environment: Windows' own, the
// user's folders and temporary folder, the proxies, and the local mirrors of docs/setup.md. PATH
// holds Windows' folders only, and nothing else of a developer's environment (INCLUDE, LIB,
// another Python's or compiler's variables) reaches the build: the toolchain action puts the
// builder's own tools on PATH (--toolchain).
const char* const kKeptVariables[] = {
    "SystemRoot", "windir", "SystemDrive", "ComSpec", "PATHEXT", "TEMP", "TMP", "USERPROFILE", "USERNAME",
    "USERDOMAIN", "COMPUTERNAME", "HOMEDRIVE", "HOMEPATH", "APPDATA", "LOCALAPPDATA", "ProgramData", "ProgramFiles",
    "ProgramFiles(x86)", "ProgramW6432", "CommonProgramFiles", "CommonProgramFiles(x86)", "CommonProgramW6432",
    "ALLUSERSPROFILE", "PUBLIC", "OS", "NUMBER_OF_PROCESSORS", "PROCESSOR_ARCHITECTURE", "PROCESSOR_IDENTIFIER",
    "PROCESSOR_LEVEL", "PROCESSOR_REVISION", "HTTP_PROXY", "HTTPS_PROXY", "NO_PROXY", "http_proxy", "https_proxy",
    "no_proxy", "TOOLCHAIN_MIRROR", "ARCHIVE_MIRROR", "GAMEINPUT_MIRROR", "DXC_MIRROR", "FFX_MIRROR"};

std::wstring EnvironmentBlock() {
  std::vector<std::wstring> variables;
  for (const char* name : kKeptVariables) {
    const std::string value = Environment(name);
    if (!value.empty()) variables.push_back(Wide(name) + L"=" + Wide(value));
  }
  wchar_t windows[MAX_PATH];
  GetWindowsDirectoryW(windows, MAX_PATH);
  const fs::path root(windows);
  variables.push_back(L"PATH=" + (root / L"System32").wstring() + L";" + root.wstring() + L";" +
                      (root / L"System32" / L"Wbem").wstring());
  std::sort(variables.begin(), variables.end(), [](const std::wstring& a, const std::wstring& b) {
    const std::wstring name_a = a.substr(0, a.find(L'=')), name_b = b.substr(0, b.find(L'='));
    return CompareStringOrdinal(name_a.c_str(), -1, name_b.c_str(), -1, TRUE) == CSTR_LESS_THAN;
  });
  std::wstring block;
  for (const auto& variable : variables) block += variable + L'\0';
  return block + L'\0';
}

// A zip member's name that the unpack takes: below the folder, no drive, no '..' (the rules of
// tool_member in pipeline.py).
bool SafeMember(const std::string& name) {
  if (name.empty() || name.front() == '/' || name.front() == '\\' || name.find(':') != std::string::npos ||
      name.find('\\') != std::string::npos)
    return false;
  std::stringstream parts(name);
  for (std::string part; std::getline(parts, part, '/');)
    if (part == ".." || part == ".") return false;
  return true;
}

void AppendLog(const fs::path& logs, const std::string& text) {
  std::error_code error;
  fs::create_directories(logs, error);
  std::ofstream stream(logs / "builder.log", std::ios::binary | std::ios::app);
  stream << Now() << "  " << text << "\r\n";
}

// What an update keeps of an install root: the folders of launcher.json builder.layout, the
// backups, the builder's state and an update's own staging folders.
bool InstallOwned(const fs::path& name) {
  static const wchar_t* const kOwned[] = {L"source",    L"build", L"toolchain",          L"downloads",  L"logs",
                                          L"backups",   L"builder-state.json", L"source.new", L"source.old",
                                          L"update.new"};
  for (const wchar_t* owned : kOwned)
    if (_wcsicmp(name.c_str(), owned) == 0) return true;
  return false;
}

std::string StampName(const SourceStamp& stamp) {
  return stamp.describe.empty() ? stamp.commit : stamp.describe + " (" + stamp.commit + ")";
}

// Copies the files below from into to, but those whose path below from (with '/') skip names.
std::string CopyTree(const fs::path& from, const fs::path& to, const std::function<bool(const std::string&)>& skip) {
  std::error_code code;
  fs::create_directories(to, code);
  if (code) return Utf8(to) + ": " + code.message();
  for (auto it = fs::recursive_directory_iterator(from, code); !code && it != fs::recursive_directory_iterator();
       it.increment(code)) {
    const fs::path relative = it->path().lexically_relative(from);
    if (skip(Utf8(fs::path(relative.generic_wstring())))) {
      if (it->is_directory()) it.disable_recursion_pending();
      continue;
    }
    std::error_code copy;
    if (it->is_directory())
      fs::create_directories(to / relative, copy);
    else
      fs::copy_file(it->path(), to / relative, fs::copy_options::overwrite_existing, copy);
    if (copy) return Utf8(it->path()) + ": " + copy.message();
  }
  return code ? Utf8(from) + ": " + code.message() : std::string();
}

}  // namespace

const char* BuilderStepTitle(const std::string& id) {
  if (id == "python") return "Python";
  if (id == "preflight") return "Check this computer";
  if (id == "toolchain") return "Build tools";
  if (id == "setup") return "Runtime";
  if (id == "generate") return "Game code from Disc 1";
  if (id == "build") return "Game";
  return "Step";
}

BuilderLayout BuilderLayout::Of(const fs::path& install, const Json& layout) {
  BuilderLayout result;
  result.install = install;
  const auto folder = [&](const char* name) {
    return (install / PathFromUtf8(layout.value(name, std::string(name)))).make_preferred();
  };
  result.source = folder("source");
  result.build = folder("build");
  result.toolchain = folder("toolchain");
  result.downloads = folder("downloads");
  result.logs = folder("logs");
  return result;
}

bool IsInstallRoot(const fs::path& directory) {
  return fs::is_regular_file(directory / "launcher.json") &&
         fs::is_regular_file(directory / "source" / "source-stamp.json") &&
         fs::is_regular_file(directory / "source" / "outputs" / "FM4-build" / "pipeline.py");
}

SourceStamp ReadStamp(const fs::path& source) {
  const Json stamp = ReadJson(source / "source-stamp.json");
  if (!stamp.is_object()) return {};
  return {stamp.value("commit", std::string()), stamp.value("describe", std::string())};
}

std::string BuiltCommit(const fs::path& build) {
  const Json report = ReadJson(build / "build-result.json");
  if (!report.is_object() || !report.contains("source") || !report.at("source").contains("head") ||
      !report.at("source").at("head").is_string())
    return {};
  return report.at("source").at("head").get<std::string>();
}

std::vector<Download> ReadDownloads(const Json& builder) {
  std::vector<Download> items;
  for (const auto& entry : builder.at("downloads")) {
    Download item;
    item.id = entry.at("id");
    item.name = entry.at("name");
    item.what = entry.value("what", std::string());
    item.host = entry.at("host");
    item.url = entry.at("url");
    item.sha256 = entry.at("sha256").is_string() ? entry.at("sha256").get<std::string>() : "";
    item.license = entry.at("license");
    item.license_link = entry.at("license_link");
    item.folder = entry.at("folder");
    item.size = entry.at("size").get<std::uint64_t>();
    item.exact = entry.at("exact").get<bool>();
    for (const auto& file : entry.at("files")) item.files.push_back(file.get<std::string>());
    item.marker = entry.at("marker");
    items.push_back(std::move(item));
  }
  return items;
}

std::uint64_t DownloadedBytes(const BuilderLayout& layout, const Download& item) {
  std::uint64_t bytes = 0;
  std::error_code error;
  for (const auto& file : item.files) {
    const fs::path path = (layout.install / PathFromUtf8(file)).make_preferred();
    // The pipeline's partial names: <file>.partial, or the file's suffix replaced.
    for (const fs::path& candidate : {path, fs::path(path).concat(L".partial"), fs::path(path).replace_extension(L".partial")}) {
      const auto size = fs::file_size(candidate, error);
      if (!error) {
        bytes += size;
        break;
      }
    }
  }
  return bytes;
}

bool Downloaded(const BuilderLayout& layout, const Download& item) {
  return std::all_of(item.files.begin(), item.files.end(),
                     [&](const std::string& file) { return fs::is_regular_file(layout.install / PathFromUtf8(file)); });
}

bool ParseProgressLine(const std::string& text, ProgressLine& line) {
  try {
    const Json json = Json::parse(text);
    if (json.value("schema", 0) != 1) return false;
    const auto text_of = [&](const char* name) {
      return json.contains(name) && json.at(name).is_string() ? json.at(name).get<std::string>() : std::string();
    };
    line.event = text_of("event");
    line.stage = text_of("stage");
    line.step = text_of("step");
    line.state = text_of("state");
    line.log = text_of("log");
    line.error = text_of("error");
    line.elapsed = json.value("elapsed", 0.0);
    return !line.event.empty() && !line.state.empty();
  } catch (const std::exception&) {
    return false;
  }
}

bool NinjaProgress(const std::string& text, int& done, int& total) {
  // Ninja writes "[12/345] Building CXX object ..." for each edge it starts or ends.
  for (size_t end = text.rfind(']'); end != std::string::npos && end > 0; end = text.rfind(']', end - 1)) {
    const size_t start = text.rfind('[', end);
    if (start == std::string::npos) return false;
    if (start > 0 && text[start - 1] != '\n') continue;
    int a = 0, b = 0;
    char tail = 0;
    if (std::sscanf(text.substr(start, end - start + 1).c_str(), "[%d/%d%c", &a, &b, &tail) == 3 && tail == ']' &&
        b > 0 && a <= b) {
      done = a;
      total = b;
      return true;
    }
  }
  return false;
}

PlainError Explain(const std::string& error, const std::string& step) {
  const auto has = [&](const char* text) { return error.find(text) != std::string::npos; };
  if (has("Unsupported executables in") || has("Missing executable:") || has("Disc1 must contain"))
    return {"These discs are not a supported release (USA or Japan/Asia), or Disc 1 is incomplete.",
            "Choose Disc 1 again on Your discs.", "discs"};
  if (has("Missing Disc 2 content package"))
    return {"Disc 2 is incomplete: content packages are missing from its content folder.",
            "Choose Disc 2 again on Your discs, or clear it to build without its cars.", "discs"};
  if (has("does not unpack to the pinned files"))
    return {"A download does not hold the files this package expects: the file on the server has changed.",
            "Retry once. When it fails again, report it: the package's pins are out of date.", "downloads"};
  if (has("pinned SHA-") || has("cannot be fetched") || has("cannot be reached") || has("the download failed") ||
      has("urlopen error") || has("getaddrinfo"))
    return {"A download failed: its server cannot be reached, or the file did not arrive whole.",
            "Check the internet connection, then Retry. When it fails again, report it.", "downloads"};
  if (has("not enough space") || has("No space left") || has("[Errno 28]") || has("ERROR_DISK_FULL") ||
      has("error 112"))
    return {"The drive is full.", "Free some space (the build needs about 20 GB), then Retry.", ""};
  if (has("could not be backed up"))
    return {"Your saves could not be backed up before the update, so the build did not start.",
            "Free some space on this drive, or close the program that holds the file named here, then Retry.", ""};
  if (has("Launcher tests failed"))
    return {"The launcher and the game of this build disagree.", "Report it: this is a bug of the project.", ""};
  if (step == "preflight")
    return {"This computer or this folder does not suit the build.", "See the checks above.", ""};
  if (has("Exit ") && has("; log:"))
    return {std::string("The build stopped in ") + BuilderStepTitle(step) + ".", "Open log, then Retry; or report it.", ""};
  return {std::string("The step ") + BuilderStepTitle(step) + " failed.", "Open log, then Retry; or report it.", ""};
}

bool OnBattery() {
  SYSTEM_POWER_STATUS status{};
  return GetSystemPowerStatus(&status) && status.ACLineStatus == 0;
}

std::string UpdateInstall(const fs::path& package, const fs::path& install) {
  std::error_code code;
  if (!IsInstallRoot(install))
    return Utf8(install) + " is not a Motorsport DX install: it has no launcher.json and source folder.";
  if (fs::equivalent(package, install, code)) return "That is this package's own folder: choose the earlier install's.";
  const SourceStamp from = ReadStamp(install / "source"), to = ReadStamp(package / "source");
  if (to.commit.empty()) return "This package has no source\\source-stamp.json: unpack its zip again.";
  // Everything is copied beside the install first, so that a failed copy leaves it as it was.
  const fs::path source = install / "source", next = install / "source.new", old = install / "source.old",
                 files = install / "update.new";
  for (const fs::path& folder : {next, old, files}) fs::remove_all(folder, code);
  const bool swap = from.commit != to.commit;
  std::string error = swap ? CopyTree(package / "source", next, [](const std::string&) { return false; }) : "";
  if (error.empty())
    error = CopyTree(package, files, [](const std::string& path) {
      const size_t slash = path.find('/');
      const fs::path top = PathFromUtf8(path.substr(0, slash));
      return InstallOwned(top) || top.extension() == L".partial";
    });
  if (error.empty() && swap && ReadStamp(next).commit != to.commit) error = "the copied source has another stamp";
  if (!error.empty()) {
    for (const fs::path& folder : {next, files}) fs::remove_all(folder, code);
    return "Unable to copy this package into " + Utf8(install) + ": " + error;
  }
  if (swap) {
    fs::rename(source, old, code);
    if (code) {
      fs::remove_all(next, code);
      fs::remove_all(files, code);
      return "Unable to replace " + Utf8(source) + " (close any program that has a file of it open): " + code.message();
    }
    fs::rename(next, source, code);
    if (code) {
      std::error_code back;
      fs::rename(old, source, back);
      fs::remove_all(files, back);
      return "Unable to replace " + Utf8(source) + ": " + code.message();
    }
    fs::remove_all(old, code);
  }
  // The launcher, launcher.json, toolchain.json, the texts and the notices. When this fails after
  // the swap, running the update again finishes it.
  error = CopyTree(files, install, [](const std::string&) { return false; });
  fs::remove_all(files, code);
  if (!error.empty())
    return "Unable to copy the package's files into " + Utf8(install) +
           " (close its launcher if it runs, then update again): " + error;
  AppendLog(install / "logs", swap ? "Updated from " + StampName(from) + " to " + StampName(to) + ", the package in " +
                                         Utf8(package)
                                   : "Copied the files of the package in " + Utf8(package) + " (" + StampName(to) +
                                         "), whose source is here already");
  return {};
}

size_t PruneSource(const fs::path& source) {
  const Json stamp = ReadJson(source / "source-stamp.json");
  if (!stamp.is_object() || !stamp.contains("paths") || !stamp.at("paths").is_array()) return 0;
  std::set<std::string> listed = {"source-stamp.json"};
  for (const auto& path : stamp.at("paths")) listed.insert(path.get<std::string>());
  std::vector<fs::path> stale, folders;
  std::error_code code;
  for (auto it = fs::recursive_directory_iterator(source, code); !code && it != fs::recursive_directory_iterator();
       it.increment(code)) {
    if (it->is_directory())
      folders.push_back(it->path());
    else if (!listed.contains(Utf8(fs::path(it->path().lexically_relative(source).generic_wstring()))))
      stale.push_back(it->path());
  }
  size_t removed = 0;
  for (const auto& path : stale) removed += fs::remove(path, code) ? 1 : 0;
  // The folders left empty, the deepest first; a folder that still holds files stays.
  for (auto it = folders.rbegin(); it != folders.rend(); ++it)
    if (fs::is_empty(*it, code)) fs::remove(*it, code);
  return removed;
}

std::string BackUpSaves(const BuildInputs& inputs, const fs::path& folder) {
  std::error_code code;
  fs::create_directories(folder, code);
  if (code) return Utf8(folder) + ": " + code.message();
  if (!inputs.user_data.empty() && fs::is_directory(inputs.user_data)) {
    // Not saves: the content imported from Disc 2 and the downloadable content (the title's
    // marketplace content, type 00000002, of the shared profile, with its headers and markers),
    // which the next Play imports again, and the game's cache.
    const std::string error = CopyTree(inputs.user_data, folder / "user-data", [](const std::string& path) {
      return path == "cache" || path == "0000000000000000/4D530910/00000002" ||
             path == "0000000000000000/4D530910/Headers/00000002" ||
             (path.find('/') == std::string::npos && path.ends_with(".import-complete"));
    });
    if (!error.empty()) return error;
  }
  if (!inputs.settings.empty())
    for (const fs::path& file : {inputs.settings, inputs.settings.parent_path() / kOverlayChangesFile})
      if (fs::is_regular_file(file)) {
        fs::copy_file(file, folder / file.filename(), fs::copy_options::overwrite_existing, code);
        if (code) return Utf8(file) + ": " + code.message();
      }
  return {};
}

std::string StartLauncher(const fs::path& program, const std::wstring& arguments) {
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION info{};
  std::wstring command = L"\"" + program.wstring() + L"\"" + (arguments.empty() ? L"" : L" " + arguments);
  if (!CreateProcessW(program.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
                      program.parent_path().c_str(), &startup, &info))
    return "Unable to start " + Utf8(program) + " (Windows error " + std::to_string(GetLastError()) + ")";
  CloseHandle(info.hThread);
  CloseHandle(info.hProcess);
  return {};
}

Builder::Builder(BuilderLayout layout, std::vector<Download> downloads, const Json& steps, SourceStamp stamp)
    : layout_(std::move(layout)), downloads_(std::move(downloads)), stamp_(std::move(stamp)) {
  for (const auto& step : steps) {
    StepView view;
    view.id = step.at("id");
    view.estimate = step.value("seconds", 0.0);
    view_.steps.push_back(view);
  }
  Refresh();
}

Builder::~Builder() {
  Pause();
  if (thread_.joinable()) thread_.join();
}

Json Builder::LoadState() const {
  Json state = ReadJson(layout_.State());
  if (!state.is_object()) state = Json::object();
  if (!state.contains("steps") || !state.at("steps").is_object()) state["steps"] = Json::object();
  return state;
}

void Builder::SaveState(const Json& state) const {
  std::error_code error;
  const fs::path temporary = fs::path(layout_.State()).concat(L".tmp");
  {
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    stream << state.dump(1);
  }
  fs::rename(temporary, layout_.State(), error);
}

fs::path Builder::Python() const {
  for (const auto& item : downloads_)
    if (item.id == "python") return (layout_.install / PathFromUtf8(item.folder)).make_preferred() / "python.exe";
  return {};
}

bool Builder::StepDone(const std::string& id, const Json& state, const BuildInputs* inputs) const {
  if (id == "build")
    return !stamp_.commit.empty() && BuiltCommit(layout_.build) == stamp_.commit &&
           fs::is_regular_file(layout_.build / "game-build" / "fm4_rex.exe") &&
           fs::is_regular_file(layout_.build / "game-build" / "MotorsportDX.exe");
  if (id == "python") {
    for (const auto& item : downloads_)
      if (item.id == "python") {
        const Json marker = ReadJson((layout_.install / PathFromUtf8(item.folder)).make_preferred() / ".fm4-tool.json");
        return marker == item.marker && fs::is_regular_file(Python());
      }
    return false;
  }
  // The other steps: done for this source in builder-state.json, with their marker.
  if (state.value("commit", std::string()) != stamp_.commit) return false;
  const Json& steps = state.at("steps");
  if (!steps.contains(id) || !steps.at(id).value("done", false)) return false;
  if (id == "toolchain") return fs::is_regular_file(layout_.toolchain / "toolchain-state.json");
  if (id == "setup") return fs::is_regular_file(layout_.build / "setup-complete.json");
  if (id == "generate") {
    if (!fs::is_regular_file(layout_.build / "local.json")) return false;
    if (!inputs) return true;
    const Json& discs = steps.at(id).contains("discs") ? steps.at(id).at("discs") : Json();
    return discs == Json{{"disc1", inputs->disc1}, {"disc2", inputs->disc2}, {"content", inputs->content}};
  }
  return true;
}

void Builder::Refresh() {
  const Json state = LoadState();
  std::lock_guard<std::mutex> lock(mutex_);
  if (shown_) return;
  bool earlier_done = true;
  for (auto& step : view_.steps) {
    // A step counts as done only after every step before it.
    const bool done = earlier_done && StepDone(step.id, state, nullptr);
    earlier_done = done;
    if (step.state == StepState::kRunning) continue;
    if (done) {
      step.state = StepState::kDone;
      const Json& steps = state.at("steps");
      step.seconds = steps.contains(step.id) ? steps.at(step.id).value("seconds", 0.0) : 0.0;
    } else if (step.state == StepState::kDone) {
      step.state = StepState::kWaiting;
    }
  }
  view_.finished = earlier_done;
  if (state.contains("failed") && state.at("failed").is_object() && view_.error.empty()) {
    view_.failed_step = state.at("failed").value("step", std::string());
    view_.error = state.at("failed").value("error", std::string());
    view_.log = state.at("failed").value("log", std::string());
    for (auto& step : view_.steps)
      if (step.id == view_.failed_step && step.state == StepState::kWaiting) step.state = StepState::kFailed;
  }
  if (state.contains("preflight") && state.at("preflight").is_array()) {
    view_.preflight.clear();
    for (const auto& line : state.at("preflight")) view_.preflight.push_back(line.get<std::string>());
  }
  jobs_ = state.value("jobs", 4);
}

BuilderView Builder::View() {
  std::lock_guard<std::mutex> lock(mutex_);
  BuilderView view = view_;
  view.busy = busy_ || (shown_ && view_.busy);
  const auto now = std::chrono::steady_clock::now();
  if (!shown_)
    for (auto& step : view.steps)
      if (step.state == StepState::kRunning)
        step.seconds = std::chrono::duration<double>(now - step_started_).count();
  return view;
}

void Builder::Show(const BuilderView& view) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto steps = view_.steps;
  view_ = view;
  if (view_.steps.empty()) view_.steps = steps;
  shown_ = true;
}

void Builder::Log(const std::string& text) { AppendLog(layout_.logs, text); }

void Builder::Start(const char* task, std::function<void()> work) {
  if (shown_ || busy_.exchange(true)) return;
  if (thread_.joinable()) thread_.join();
  cancel_ = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    view_.task = task;
    view_.message.clear();
  }
  thread_ = std::thread([this, work = std::move(work)] {
    // The computer stays awake while the builder works; no setting changes.
    SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED);
    try {
      work();
    } catch (const std::exception& exception) {
      Fail("", exception.what());
    }
    SetThreadExecutionState(ES_CONTINUOUS);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      view_.task.clear();
      view_.downloading.clear();
      view_.detail.clear();
      view_.ninja_done = view_.ninja_total = 0;
    }
    busy_ = false;
    Refresh();
  });
}

void Builder::Pause() {
  cancel_ = true;
  std::lock_guard<std::mutex> lock(mutex_);
  if (job_) TerminateJobObject(job_, 1);
}

void Builder::Fail(const std::string& step, const std::string& error) {
  Log("Failed" + (step.empty() ? std::string() : " in " + step) + ": " + error);
  Json state = LoadState();
  std::string log;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    view_.error = error;
    view_.failed_step = step;
    log = view_.log;
    for (auto& view : view_.steps)
      if (view.id == step) view.state = StepState::kFailed;
  }
  state["failed"] = Json{{"step", step}, {"error", error}, {"log", log}, {"time", Now()}};
  SaveState(state);
}

std::string Builder::FetchPython() {
  const auto item = std::find_if(downloads_.begin(), downloads_.end(), [](const Download& d) { return d.id == "python"; });
  if (item == downloads_.end()) return "launcher.json names no Python.";
  const fs::path archive = (layout_.install / PathFromUtf8(item->files.front())).make_preferred();
  std::vector<std::string> sources;
  const std::string mirror = Environment("TOOLCHAIN_MIRROR");
  if (!mirror.empty() && fs::is_regular_file(PathFromUtf8(mirror) / archive.filename()))
    sources.push_back(Utf8(PathFromUtf8(mirror) / archive.filename()));
  sources.push_back(item->url);
  const std::string failures = FetchPinned(sources, item->sha256, archive);
  if (!failures.empty()) return item->name + " cannot be fetched with its pinned SHA-256:" + failures;
  Log(item->name + ": " + Utf8(archive) + ", SHA-256 " + item->sha256 + " checked");
  // Unpacked as the pipeline unpacks a tool (unpack_tool): every member's name checked first, into
  // a .partial folder, then moved into place with install_tool's marker.
  std::string listing;
  std::string error = RunTar({L"-tf", archive.wstring()}, &listing);
  if (!error.empty()) return item->name + ": " + error;
  std::vector<std::string> members;
  std::stringstream lines(listing);
  for (std::string line; std::getline(lines, line);) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) continue;
    if (!SafeMember(line)) return item->name + ": refused member " + line;
    if (line.back() != '/') members.push_back(line);
  }
  const fs::path folder = (layout_.install / PathFromUtf8(item->folder)).make_preferred();
  const fs::path partial = fs::path(folder).concat(L".partial");
  std::error_code code;
  fs::remove_all(partial, code);
  fs::create_directories(partial, code);
  error = RunTar({L"-xf", archive.wstring(), L"-C", partial.wstring()});
  if (!error.empty()) return item->name + ": " + error;
  for (const auto& member : members)
    if (!fs::is_regular_file(partial / PathFromUtf8(member))) return item->name + ": " + member + " was not unpacked";
  {
    std::ofstream marker(partial / ".fm4-tool.json", std::ios::binary | std::ios::trunc);
    marker << item->marker.dump();
  }
  fs::remove_all(folder, code);
  fs::rename(partial, folder, code);
  if (code) return item->name + ": " + Utf8(folder) + ": " + code.message();
  Log(item->name + ": " + std::to_string(members.size()) + " files in " + Utf8(folder));
  return {};
}

void Builder::ReadProgress() {
  std::ifstream stream(ProgressFile(), std::ios::binary);
  if (!stream) return;
  stream.seekg(static_cast<std::streamoff>(progress_offset_));
  std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
  const size_t end = text.rfind('\n');
  if (end == std::string::npos) return;
  progress_offset_ += end + 1;
  std::stringstream lines(text.substr(0, end));
  std::lock_guard<std::mutex> lock(mutex_);
  for (std::string line; std::getline(lines, line);) {
    ProgressLine progress;
    if (!ParseProgressLine(line, progress)) continue;
    if (progress.event == "step" && progress.state == "running") {
      view_.detail = progress.step;
      if (!progress.log.empty()) view_.log = progress.log;
      view_.ninja_done = view_.ninja_total = 0;
    }
    if (progress.state == "failed" && !progress.error.empty() && view_.error.empty()) {
      view_.error = progress.error;
      if (!progress.log.empty()) view_.log = progress.log;
    }
  }
}

long Builder::Run(const std::vector<std::string>& args, const fs::path& log) {
  Log("Running: " + CommandLine(args));
  std::error_code code;
  fs::create_directories(log.parent_path(), code);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    view_.log = Utf8(log);
  }
  SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
  HANDLE output = CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
  if (output == INVALID_HANDLE_VALUE) throw std::runtime_error("Unable to write " + Utf8(log));
  // A job object: Pause and the launcher's exit end the pipeline with every compiler it started.
  HANDLE job = CreateJobObjectW(nullptr, nullptr);
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdOutput = output;
  startup.hStdError = output;
  startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  PROCESS_INFORMATION info{};
  std::wstring command = Wide(CommandLine(args)), environment = EnvironmentBlock();
  const BOOL started = CreateProcessW(Wide(args.front()).c_str(), command.data(), nullptr, nullptr, TRUE,
                                      CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED | CREATE_NO_WINDOW,
                                      environment.data(), layout_.install.c_str(), &startup, &info);
  const DWORD start_error = GetLastError();
  CloseHandle(output);
  if (!started) {
    CloseHandle(job);
    throw std::runtime_error("Unable to start " + args.front() + " (Windows error " + std::to_string(start_error) + ")");
  }
  AssignProcessToJobObject(job, info.hProcess);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    job_ = job;
  }
  if (cancel_) TerminateJobObject(job, 1);
  ResumeThread(info.hThread);
  CloseHandle(info.hThread);
  while (WaitForSingleObject(info.hProcess, 250) == WAIT_TIMEOUT) {
    ReadProgress();
    std::string running;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      running = view_.log;
    }
    int done = 0, total = 0;
    if (!running.empty() && NinjaProgress(ReadText(PathFromUtf8(running), 8192), done, total)) {
      std::lock_guard<std::mutex> lock(mutex_);
      view_.ninja_done = done;
      view_.ninja_total = total;
    }
  }
  ReadProgress();
  DWORD exit = 0;
  GetExitCodeProcess(info.hProcess, &exit);
  CloseHandle(info.hProcess);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    job_ = nullptr;
  }
  CloseHandle(job);
  Log("Exit " + std::to_string(exit) + (cancel_ ? " (paused)" : ""));
  return cancel_ ? -1 : static_cast<long>(exit);
}

bool Builder::Fetch(const std::string& id) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    view_.downloading = id;
    view_.error.clear();
  }
  if (id == "python") {
    const std::string error = FetchPython();
    if (!error.empty()) Fail("python", error);
    return error.empty();
  }
  if (!fs::is_regular_file(Python())) {
    Fail("", "Download Python first: the other downloads run on it.");
    return false;
  }
  {
    std::error_code code;
    progress_offset_ = fs::file_size(ProgressFile(), code);
    if (code) progress_offset_ = 0;
  }
  const fs::path log = layout_.logs / ("download-" + id + ".log");
  const long exit = Run({Utf8(Python()), "-I", Utf8(layout_.Pipeline()), "download", "--item", id, "--build-root",
                         Utf8(layout_.build), "--downloads", Utf8(layout_.downloads), "--progress", Utf8(ProgressFile())},
                        log);
  if (exit == -1) return false;
  if (exit != 0) {
    std::string error;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      error = view_.error;
    }
    Fail("", error.empty() ? "The download of " + id + " stopped (exit " + std::to_string(exit) + "); log: " + Utf8(log)
                           : error);
    return false;
  }
  return true;
}

void Builder::StartDownloads(std::vector<std::string> ids) {
  Start("downloads", [this, ids = std::move(ids)] {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      view_.error.clear();
      view_.failed_step.clear();
    }
    for (const auto& item : downloads_) {
      if (std::find(ids.begin(), ids.end(), item.id) == ids.end() || Downloaded(layout_, item)) continue;
      if (cancel_ || !Fetch(item.id)) return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    view_.message = "The downloads are done.";
  });
}

bool Builder::RunStep(size_t index, const BuildInputs& inputs) {
  std::string id;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    id = view_.steps[index].id;
    view_.steps[index].state = StepState::kRunning;
    view_.detail.clear();
    view_.error.clear();
    view_.failed_step.clear();
  }
  step_started_ = std::chrono::steady_clock::now();
  std::error_code code;
  progress_offset_ = fs::file_size(ProgressFile(), code);
  if (code) progress_offset_ = 0;
  Json state = LoadState();
  state["commit"] = stamp_.commit;
  state["describe"] = stamp_.describe;
  state.erase("failed");
  // This step and every later one are redone.
  bool from_here = false;
  for (const auto& step : view_.steps) {
    from_here = from_here || step.id == id;
    if (from_here) state["steps"].erase(step.id);
  }
  SaveState(state);
  Log("Step " + id + " (" + BuilderStepTitle(id) + ")");
  const std::string python = Utf8(Python()), pipeline = Utf8(layout_.Pipeline());
  const std::string progress = Utf8(ProgressFile());
  const fs::path log = layout_.logs / (id + ".log");
  std::vector<std::string> args = {python, "-I", pipeline, id};
  if (id != "python") args.insert(args.end(), {"--build-root", Utf8(layout_.build)});
  long exit = 0;
  std::string error;
  if (id == "python") {
    error = FetchPython();
  } else if (id == "preflight") {
    exit = Run(args, log);
    // preflight prints one JSON object, and exits with 1 when a check fails.
    const std::string text = ReadText(log);
    const size_t start = text.find('{'), end = text.rfind('}');
    Json report;
    try {
      if (start != std::string::npos && end != std::string::npos) report = Json::parse(text.substr(start, end - start + 1));
    } catch (const std::exception&) {
    }
    std::vector<std::string> lines;
    if (report.is_object()) {
      jobs_ = std::clamp(report.value("suggested_jobs", 4), 1, 64);
      for (const auto& [name, check] : report.at("checks").items()) {
        const std::string check_state = check.value("state", std::string());
        if (check_state == "pass") continue;
        std::string line = name + ": " + check_state;
        if (check.contains("detail")) line += ", " + check.at("detail").get<std::string>();
        if (check.contains("missing") && !check.at("missing").empty()) line += ", missing " + check.at("missing").dump();
        for (const char* role : {"build_root", "source"})
          if (check.contains(role) && check.at(role).contains("problems"))
            for (const auto& problem : check.at(role).at("problems"))
              line += ", " + std::string(role) + ": " + problem.get<std::string>();
        if (name == "disk" && check.contains("free_bytes"))
          line += ", " + std::to_string(check.at("free_bytes").get<std::uint64_t>() / 1000000000) + " GB free of " +
                  std::to_string(check.value("required_bytes", 0ull) / 1000000000) + " GB needed";
        lines.push_back(line);
      }
    }
    state["preflight"] = lines;
    state["jobs"] = jobs_;
    SaveState(state);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      view_.preflight = lines;
    }
    if (exit > 0) error = report.is_object() ? "preflight: a check failed" : "preflight: Exit " + std::to_string(exit) + "; log: " + Utf8(log);
  } else {
    if (id == "toolchain")
      args.insert(args.end(), {"--toolchain", Utf8(layout_.toolchain), "--downloads", Utf8(layout_.downloads), "--portable"});
    else
      args.insert(args.end(), {"--toolchain", Utf8(layout_.toolchain), "--jobs", std::to_string(jobs_)});
    if (id == "generate") {
      args.insert(args.end(), {"--disc1", inputs.disc1});
      if (!inputs.disc2.empty()) args.insert(args.end(), {"--disc2", inputs.disc2});
      for (const auto& content : inputs.content) args.insert(args.end(), {"--content", content});
    }
    if (id == "build") args.insert(args.end(), {"--tests", "player"});
    args.insert(args.end(), {"--progress", progress});
    exit = Run(args, log);
    if (exit > 0) {
      std::lock_guard<std::mutex> lock(mutex_);
      error = view_.error;
      if (error.empty()) {
        // The pipeline's own message, its last line "ERROR: ...".
        const std::string text = ReadText(log, 4096);
        const size_t at = text.rfind("ERROR: ");
        error = at != std::string::npos ? text.substr(at + 7) : "Exit " + std::to_string(exit) + "; log: " + Utf8(log);
        while (!error.empty() && (error.back() == '\n' || error.back() == '\r')) error.pop_back();
      }
    }
  }
  if (exit == -1) {
    std::lock_guard<std::mutex> lock(mutex_);
    view_.steps[index].state = StepState::kPaused;
    Log("Paused in " + id);
    return false;
  }
  if (!error.empty()) {
    Fail(id, error);
    return false;
  }
  const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - step_started_).count();
  state = LoadState();
  state["steps"][id] = Json{{"done", true}, {"seconds", seconds}, {"time", Now()}};
  if (id == "generate")
    state["steps"][id]["discs"] = Json{{"disc1", inputs.disc1}, {"disc2", inputs.disc2}, {"content", inputs.content}};
  SaveState(state);
  Log("Done " + id + " in " + std::to_string(static_cast<int>(seconds)) + " s");
  std::lock_guard<std::mutex> lock(mutex_);
  view_.steps[index].state = StepState::kDone;
  view_.steps[index].seconds = seconds;
  return true;
}

bool Builder::PrepareUpdate(const BuildInputs& inputs) {
  if (const size_t removed = PruneSource(layout_.source))
    Log("Removed " + std::to_string(removed) + " files of an earlier package from " + Utf8(layout_.source));
  const std::string built = BuiltCommit(layout_.build);
  if (built.empty() || built == stamp_.commit) return true;
  Json state = LoadState();
  if (state.value("backup_for", std::string()) == stamp_.commit) return true;
  // backups\<date and time>-<the earlier build's commit>: the saves are not checked between builds.
  std::string date = Now();
  std::replace(date.begin(), date.end(), ' ', '-');
  date.erase(std::remove(date.begin(), date.end(), ':'), date.end());
  const fs::path folder = layout_.Backups() / (date + "-" + built.substr(0, 7));
  const std::string error = BackUpSaves(inputs, folder);
  if (!error.empty()) {
    Fail("", "The saves could not be backed up before the update: " + error);
    return false;
  }
  Log("Update from the build of " + built + ": the saves backed up into " + Utf8(folder));
  state["backup_for"] = stamp_.commit;
  state["backup"] = Utf8(folder);
  SaveState(state);
  return true;
}

void Builder::StartBuild(const BuildInputs& inputs, bool download_all) {
  Start("build", [this, inputs, download_all] {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      view_.error.clear();
      view_.failed_step.clear();
    }
    Log("Build of " + stamp_.describe + " (" + stamp_.commit + ") in " + Utf8(layout_.install));
    if (download_all) {
      // Python first: the other downloads run on it.
      for (const auto& item : downloads_)
        if (!Downloaded(layout_, item) && (cancel_ || !Fetch(item.id))) return;
    } else {
      for (const auto& item : downloads_)
        if (item.id != "python" && !Downloaded(layout_, item)) {
          Fail("", "Not downloaded yet: " + item.name + ". Download it on the Downloads screen.");
          return;
        }
    }
    if (cancel_ || !PrepareUpdate(inputs)) return;
    const Json state = LoadState();
    bool earlier_done = true;
    for (size_t index = 0; index < view_.steps.size(); ++index) {
      const std::string id = view_.steps[index].id;
      earlier_done = earlier_done && StepDone(id, state, &inputs);
      if (earlier_done) continue;
      if (cancel_ || !RunStep(index, inputs)) return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    view_.finished = true;
    view_.message = "The game is built.";
    Log("Build finished");
  });
}

std::string Builder::HandOver() const { return StartLauncher(layout_.build / "game-build" / "MotorsportDX.exe"); }

bool Builder::Built() const { return StepDone("build", Json::object(), nullptr); }

std::string Builder::Details() {
  const BuilderView view = View();
  std::string text = "Motorsport DX builder\r\n";
  text += "Package: " + (stamp_.describe.empty() ? std::string("unknown") : stamp_.describe) + " (" + stamp_.commit + ")\r\n";
  OSVERSIONINFOEXW version{sizeof(version)};
  using RtlGetVersion = LONG(WINAPI*)(OSVERSIONINFOEXW*);
  if (const auto get = reinterpret_cast<RtlGetVersion>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion")))
    get(&version);
  text += "Windows: " + std::to_string(version.dwMajorVersion) + "." + std::to_string(version.dwMinorVersion) + "." +
          std::to_string(version.dwBuildNumber) + "\r\n";
  text += "Step: " + (view.failed_step.empty() ? std::string("-") : std::string(BuilderStepTitle(view.failed_step)) + " (" +
                                                                         view.failed_step + ")") + "\r\n";
  text += "Error: " + view.error + "\r\n";
  const Json tools = ReadJson(layout_.toolchain / "toolchain-state.json");
  if (tools.is_object() && tools.contains("tools"))
    for (const auto& [name, tool] : tools.at("tools").items())
      text += "Tool: " + name + " " + tool.value("version", std::string()) + " (" + tool.value("source", std::string()) +
              ")\r\n";
  for (const auto& line : view.preflight) text += "Preflight: " + line + "\r\n";
  text += "Logs to attach: " + Utf8(layout_.logs / "builder.log");
  if (!view.log.empty()) text += ", " + view.log;
  text += "\r\nThe build's logs: " + Utf8(layout_.build / "logs") + "\r\n";
  return text;
}

}  // namespace fm4
