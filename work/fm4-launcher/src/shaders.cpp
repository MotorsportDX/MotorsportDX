#include "shaders.h"

#include <windows.h>
#include <urlmon.h>

#include <fstream>
#include <system_error>
#include <vector>

#include "discs.h"
#include "plan.h"

namespace fm4 {

namespace fs = std::filesystem;

namespace {

// pipeline.py DXC_URL, DXC_SHA256 and DXC_FILES.
constexpr const char* kDxcUrl =
    "https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.9.2609/"
    "dxc_2026_09_29.zip";
constexpr const char* kDxcSha256 = "ad31b1fc8443175d204f77a611fdb3ef2ec42759bdc2f1167368de24a4a7e7f1";
struct DxcFile {
  const char* member;
  const char* sha256;  // nullptr: a license file
};
constexpr DxcFile kDxcFiles[] = {
    {"bin/x64/dxcompiler.dll", "0689e2fee0a1c9df8662a59680469f24e2a3ad7ce6bbbcc9284f7a83172bfc59"},
    {"bin/x64/dxil.dll", "64be2368211d257a038a7d4ea6bb3e24c878eb8d588c7b8414fccf759b69c2ac"},
    {"LICENSE-MS.txt", nullptr},
    {"LICENSE-LLVM.txt", nullptr},
    {"LICENCE-MIT.txt", nullptr},
};

std::wstring Wide(const std::string& text) {
  if (text.empty()) return {};
  const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
  std::wstring wide(static_cast<size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), size);
  return wide;
}

}  // namespace

std::string RunTar(const std::vector<std::wstring>& args, std::string* output) {
  wchar_t system[MAX_PATH];
  if (!GetSystemDirectoryW(system, MAX_PATH)) return "the system directory is unknown";
  std::wstring command = L"\"" + (fs::path(system) / L"tar.exe").wstring() + L"\"";
  for (const std::wstring& arg : args) command += L" \"" + arg + L"\"";
  SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
  HANDLE read = nullptr, write = nullptr;
  if (output && !CreatePipe(&read, &write, &inherit, 0)) return "tar.exe: no pipe";
  if (read) SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0);
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  if (write) {
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = write;
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
  }
  PROCESS_INFORMATION info{};
  const BOOL started = CreateProcessW(nullptr, command.data(), nullptr, nullptr, write != nullptr, CREATE_NO_WINDOW,
                                      nullptr, nullptr, &startup, &info);
  const DWORD start_error = GetLastError();
  if (write) CloseHandle(write);
  if (!started) {
    if (read) CloseHandle(read);
    return "tar.exe does not start (Windows error " + std::to_string(start_error) + ")";
  }
  if (read) {
    char buffer[4096];
    DWORD count = 0;
    while (ReadFile(read, buffer, sizeof(buffer), &count, nullptr) && count) output->append(buffer, count);
    CloseHandle(read);
  }
  WaitForSingleObject(info.hProcess, INFINITE);
  DWORD code = 1;
  GetExitCodeProcess(info.hProcess, &code);
  CloseHandle(info.hThread);
  CloseHandle(info.hProcess);
  return code ? "tar.exe failed (exit " + std::to_string(code) + ")" : "";
}

std::string FetchPinned(const std::vector<std::string>& sources, const std::string& sha256, const fs::path& target) {
  std::error_code error;
  if (fs::is_regular_file(target) && Sha256File(target) == sha256) return {};
  fs::create_directories(target.parent_path(), error);
  const fs::path partial = fs::path(target).concat(L".partial");
  std::string failures;
  for (const std::string& source : sources) {
    fs::remove(partial, error);
    if (fs::is_regular_file(PathFromUtf8(source))) {
      fs::copy_file(PathFromUtf8(source), partial, fs::copy_options::overwrite_existing, error);
      if (error) {
        failures += "\n" + source + ": " + error.message();
        continue;
      }
    } else if (URLDownloadToFileW(nullptr, Wide(source).c_str(), partial.c_str(), 0, nullptr) != S_OK) {
      failures += "\n" + source + ": the download failed";
      continue;
    }
    if (Sha256File(partial) != sha256) {
      failures += "\n" + source + ": not the pinned SHA-256";
      fs::remove(partial, error);
      continue;
    }
    fs::rename(partial, target, error);
    if (!error) return {};
    failures += "\n" + Utf8(target) + ": " + error.message();
  }
  fs::remove(partial, error);
  return failures.empty() ? "\nno source" : failures;
}

bool DxcPresent(const fs::path& game) {
  return fs::is_regular_file(game / "dxcompiler.dll") && fs::is_regular_file(game / "dxil.dll");
}

std::string FetchDxc(const fs::path& game, const fs::path& work) {
  std::error_code error;
  fs::create_directories(work, error);
  const fs::path archive = work / "dxc_2026_09_29.zip";
  std::vector<std::string> sources;
  char mirror[2048];
  const DWORD length = GetEnvironmentVariableA("FM4_DXC_MIRROR", mirror, sizeof(mirror));
  if (length && length < sizeof(mirror)) sources.emplace_back(mirror);
  sources.emplace_back(kDxcUrl);
  const std::string failures = FetchPinned(sources, kDxcSha256, archive);
  if (!failures.empty()) return "The DirectX Shader Compiler could not be fetched:" + failures;
  const fs::path extracted = work / "extracted";
  fs::remove_all(extracted, error);
  fs::create_directories(extracted, error);
  std::string failure = RunTar({L"-xf", archive.wstring(), L"-C", extracted.wstring()});
  if (!failure.empty()) failure += " extracting the release";
  if (failure.empty()) {
    for (const DxcFile& file : kDxcFiles) {
      const fs::path from = extracted / PathFromUtf8(file.member);
      if (!fs::is_regular_file(from)) {
        failure = std::string("the release has no ") + file.member;
        break;
      }
      if (file.sha256 && Sha256File(from) != file.sha256) {
        failure = std::string(file.member) + " is not the pinned file";
        break;
      }
      fs::copy_file(from, game / from.filename(), fs::copy_options::overwrite_existing, error);
      if (error) {
        failure = "cannot write " + Utf8(game / from.filename()) + ": " + error.message();
        break;
      }
    }
  }
  fs::remove_all(extracted, error);
  return failure.empty() ? "" : "The DirectX Shader Compiler: " + failure;
}

bool ReadPassProgress(const fs::path& file, PassProgress& out) {
  std::ifstream stream(file, std::ios::binary);
  std::string line, last;
  while (std::getline(stream, line))
    if (!line.empty()) last = line;
  if (last.empty()) return false;
  try {
    const Json json = Json::parse(last);
    out.stage = json.value("stage", std::string());
    out.done = json.value("done", 0LL);
    out.total = json.value("total", 0LL);
    out.errors = json.value("errors", 0LL);
    out.finished = json.value("finished", false);
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

}  // namespace fm4
