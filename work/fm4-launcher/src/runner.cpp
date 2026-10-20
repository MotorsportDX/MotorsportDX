#include "runner.h"

#include <windows.h>
#include <shellapi.h>
#include <shellscalingapi.h>
#include <tlhelp32.h>

#include <algorithm>

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

std::string LastError(const std::string& what) {
  return what + " (Windows error " + std::to_string(GetLastError()) + ")";
}

}  // namespace

Monitor PrimaryMonitor() {
  Monitor monitor;
  monitor.width = GetSystemMetrics(SM_CXSCREEN);
  monitor.height = GetSystemMetrics(SM_CYSCREEN);
  UINT dpi_x = 96, dpi_y = 96;
  const HMONITOR handle = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
  monitor.dpi = GetDpiForMonitor(handle, MDT_EFFECTIVE_DPI, &dpi_x, &dpi_y) == S_OK ? dpi_x / 96.0 : 1.0;
  return monitor;
}

std::string CommandLine(const std::vector<std::string>& args) {
  std::string result;
  for (const auto& arg : args) {
    if (!result.empty()) result += ' ';
    const bool quote = arg.empty() || arg.find_first_of(" \t") != std::string::npos;
    if (quote) result += '"';
    size_t backslashes = 0;
    for (char c : arg) {
      if (c == '\\') {
        ++backslashes;
      } else if (c == '"') {
        result.append(backslashes * 2, '\\');
        backslashes = 0;
        result += "\\\"";
      } else {
        result.append(backslashes, '\\');
        backslashes = 0;
        result += c;
      }
    }
    result.append(backslashes, '\\');
    if (quote) {
      result.append(backslashes, '\\');
      result += '"';
    }
  }
  return result;
}

void* StartProcess(const std::vector<std::string>& args, const Pairs& env, const fs::path& directory,
                   const fs::path& log) {
  // The launcher's environment without its FM4_ variables, then the plan's.
  std::vector<std::wstring> variables;
  if (wchar_t* block = GetEnvironmentStringsW()) {
    for (const wchar_t* item = block; *item; item += wcslen(item) + 1) {
      const std::wstring text(item);
      if (_wcsnicmp(text.c_str(), L"FM4_", 4) != 0) variables.push_back(text);
    }
    FreeEnvironmentStringsW(block);
  }
  for (const auto& [name, value] : env.Items()) variables.push_back(Wide(name) + L"=" + Wide(value));
  std::sort(variables.begin(), variables.end(), [](const std::wstring& a, const std::wstring& b) {
    const std::wstring name_a = a.substr(0, a.find(L'=', 1)), name_b = b.substr(0, b.find(L'=', 1));
    return CompareStringOrdinal(name_a.c_str(), -1, name_b.c_str(), -1, TRUE) == CSTR_LESS_THAN;
  });
  std::wstring block;
  for (const auto& variable : variables) block += variable + L'\0';
  block += L'\0';

  fs::create_directories(log.parent_path());
  SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
  HANDLE output = CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (output == INVALID_HANDLE_VALUE) throw std::runtime_error(LastError("Unable to write " + Utf8(log)));
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdOutput = output;
  startup.hStdError = output;
  PROCESS_INFORMATION info{};
  std::wstring command = Wide(CommandLine(args));
  const std::wstring application = Wide(args.front());
  const BOOL started = CreateProcessW(application.c_str(), command.data(), nullptr, nullptr, TRUE,
                                      CREATE_UNICODE_ENVIRONMENT, block.data(),
                                      directory.c_str(), &startup, &info);
  const std::string error = started ? "" : LastError("Unable to start " + args.front());
  CloseHandle(output);
  if (!started) throw std::runtime_error(error);
  CloseHandle(info.hThread);
  return info.hProcess;
}

unsigned long WaitProcess(void* process) {
  WaitForSingleObject(process, INFINITE);
  DWORD code = 0;
  GetExitCodeProcess(process, &code);
  CloseHandle(process);
  return code;
}

void OpenPath(const fs::path& path) {
  ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

bool ProcessRunning(const wchar_t* name) {
  const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return false;
  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  bool found = false;
  for (BOOL more = Process32FirstW(snapshot, &entry); more && !found; more = Process32NextW(snapshot, &entry))
    found = _wcsicmp(entry.szExeFile, name) == 0;
  CloseHandle(snapshot);
  return found;
}

}  // namespace fm4
