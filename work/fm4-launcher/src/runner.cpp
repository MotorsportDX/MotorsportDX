#include "runner.h"

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <shellapi.h>
#include <shellscalingapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <tuple>
#include <utility>

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

// pipeline.py vrr_range: DisplayID's Adaptive-Sync data block, else EDID 1.4's range limits
// with the continuous frequency bit.
std::pair<int, int> VrrRange(const std::vector<unsigned char>& edid) {
  std::pair<int, int> best{0, 0};
  for (size_t start = 128; start + 128 <= edid.size(); start += 128) {
    const unsigned char* ext = edid.data() + start;
    if (ext[0] != 0x70) continue;
    const size_t end = std::min<size_t>(5 + ext[2], 127);
    for (size_t i = 5; i + 3 <= end; i += 3 + ext[i + 2]) {
      if (ext[i] != 0x2B) continue;
      const size_t blocks_end = std::min<size_t>(i + 3 + ext[i + 2], end);
      for (size_t d = i + 3; d + 6 <= blocks_end; d += 6) {
        const int low = ext[d + 2], high = (ext[d + 3] | ((ext[d + 4] & 3) << 8)) + 1;
        if (high > best.second) best = {low, high};
      }
    }
  }
  if (best.second) return best;
  if (edid.size() >= 128 && edid[19] >= 4 && (edid[0x18] & 1)) {
    for (size_t d = 54; d < 126; d += 18) {
      const unsigned char* block = edid.data() + d;
      if (!block[0] && !block[1] && !block[2] && block[3] == 0xFD) {
        return {block[5] + ((block[4] & 1) ? 255 : 0), block[6] + ((block[4] & 2) ? 255 : 0)};
      }
    }
  }
  return {0, 0};
}

// pipeline.py primary_display: the primary monitor's refresh rate and the VRR range of its
// EDID in the registry.
void PrimaryDisplay(Monitor& monitor) {
  DISPLAY_DEVICEW adapter{};
  adapter.cb = sizeof(adapter);
  bool found = false;
  for (DWORD index = 0; EnumDisplayDevicesW(nullptr, index, &adapter, 0); ++index) {
    if (adapter.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) {
      found = true;
      break;
    }
  }
  if (!found) return;
  DEVMODEW mode{};
  mode.dmSize = sizeof(mode);
  if (EnumDisplaySettingsW(adapter.DeviceName, ENUM_CURRENT_SETTINGS, &mode))
    monitor.refresh = static_cast<int>(mode.dmDisplayFrequency);
  DISPLAY_DEVICEW display{};
  display.cb = sizeof(display);
  // \\?\DISPLAY#<model>#<instance>#{guid}.
  if (!EnumDisplayDevicesW(adapter.DeviceName, 0, &display, EDD_GET_DEVICE_INTERFACE_NAME)) return;
  std::vector<std::wstring> parts;
  std::wstring part;
  for (const wchar_t c : std::wstring(display.DeviceID)) {
    if (c == L'#') {
      parts.push_back(part);
      part.clear();
    } else {
      part += c;
    }
  }
  parts.push_back(part);
  if (parts.size() < 3) return;
  const std::wstring key =
      L"SYSTEM\\CurrentControlSet\\Enum\\DISPLAY\\" + parts[1] + L"\\" + parts[2] + L"\\Device Parameters";
  DWORD size = 0;
  if (RegGetValueW(HKEY_LOCAL_MACHINE, key.c_str(), L"EDID", RRF_RT_REG_BINARY, nullptr, nullptr,
                   &size) != ERROR_SUCCESS)
    return;
  std::vector<unsigned char> edid(size);
  if (RegGetValueW(HKEY_LOCAL_MACHINE, key.c_str(), L"EDID", RRF_RT_REG_BINARY, nullptr, edid.data(),
                   &size) != ERROR_SUCCESS)
    return;
  edid.resize(size);
  std::tie(monitor.vrr_min, monitor.vrr_max) = VrrRange(edid);
}

// As pipeline.py integrated_gpu: every hardware adapter is UMA (D3D12_FEATURE_ARCHITECTURE of a
// device made on it), or has under 1 GB of dedicated video memory where no device can be made.
bool IntegratedGpu() {
  const HMODULE d3d12 = LoadLibraryW(L"d3d12.dll");
  if (!d3d12) return false;
  const auto create_device =
      reinterpret_cast<PFN_D3D12_CREATE_DEVICE>(GetProcAddress(d3d12, "D3D12CreateDevice"));
  IDXGIFactory1* factory = nullptr;
  if (!create_device || FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
    FreeLibrary(d3d12);
    return false;
  }
  int hardware = 0, integrated = 0;
  IDXGIAdapter1* adapter = nullptr;
  for (UINT index = 0; factory->EnumAdapters1(index, &adapter) == S_OK; ++index) {
    DXGI_ADAPTER_DESC1 desc{};
    if (SUCCEEDED(adapter->GetDesc1(&desc)) && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
      ++hardware;
      bool uma = desc.DedicatedVideoMemory < (SIZE_T(1) << 30);
      ID3D12Device* device = nullptr;
      if (SUCCEEDED(create_device(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) {
        D3D12_FEATURE_DATA_ARCHITECTURE architecture{};
        if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE, &architecture,
                                                  sizeof(architecture))))
          uma = architecture.UMA != FALSE;
        device->Release();
      }
      integrated += uma;
    }
    adapter->Release();
  }
  factory->Release();
  FreeLibrary(d3d12);
  return hardware > 0 && integrated == hardware;
}

}  // namespace

Monitor PrimaryMonitor() {
  Monitor monitor;
  monitor.width = GetSystemMetrics(SM_CXSCREEN);
  monitor.height = GetSystemMetrics(SM_CYSCREEN);
  UINT dpi_x = 96, dpi_y = 96;
  const HMONITOR handle = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
  monitor.dpi = GetDpiForMonitor(handle, MDT_EFFECTIVE_DPI, &dpi_x, &dpi_y) == S_OK ? dpi_x / 96.0 : 1.0;
  PrimaryDisplay(monitor);
  monitor.integrated_gpu = IntegratedGpu();
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
