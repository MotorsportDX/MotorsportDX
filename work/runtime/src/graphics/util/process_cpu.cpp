#include "process_cpu.h"

#include <algorithm>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <rex/cvar.h>
#include <rex/platform.h>

#if REX_PLATFORM_WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#endif

REXCVAR_DEFINE_BOOL(perf_trace_cpu, true, "GPU",
                    "With the perf trace: the process's CPU time and its threads' by name in each "
                    "window (process_cpu; docs/analysis/d3d-hle-n4.md 17.19)");

namespace rex::graphics::perf_trace {

bool ProcessCpuEnabled() { return REXCVAR_GET(perf_trace_cpu); }

#if REX_PLATFORM_WIN32
namespace {

double FiletimeMs(const FILETIME& time) {
  return double(uint64_t(time.dwHighDateTime) << 32 | time.dwLowDateTime) / 10000.0;
}

struct Thread {
  DWORD id = 0;
  HANDLE handle = nullptr;
  std::string name;
  double last_ms = 0;
};

std::string ThreadName(HANDLE handle) {
  PWSTR text = nullptr;
  std::string name = "thread";
  if (SUCCEEDED(GetThreadDescription(handle, &text)) && text && *text) {
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (size > 1) {
      name.assign(size_t(size - 1), '\0');
      WideCharToMultiByte(CP_UTF8, 0, text, -1, name.data(), size, nullptr, nullptr);
    }
  }
  if (text) LocalFree(text);
  return name;
}

double ThreadMs(HANDLE handle) {
  FILETIME created, exited, kernel, user;
  if (!GetThreadTimes(handle, &created, &exited, &kernel, &user)) return -1;
  return FiletimeMs(kernel) + FiletimeMs(user);
}

// One sample: the time since the previous one.
void Sample(std::ostream& output) {
  static std::vector<Thread> threads;
  static uint32_t calls = 0;
  static double last_process_ms = -1;
  // The ms of the threads that exited since the last call, by name.
  std::map<std::string, std::pair<double, int>> by_name;
  if (calls++ % 10 == 0) {
    const DWORD pid = GetCurrentProcessId();
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
      std::vector<DWORD> alive;
      THREADENTRY32 entry{};
      entry.dwSize = sizeof(entry);
      for (BOOL more = Thread32First(snapshot, &entry); more; more = Thread32Next(snapshot, &entry)) {
        if (entry.th32OwnerProcessID != pid) continue;
        alive.push_back(entry.th32ThreadID);
        const bool known = std::any_of(threads.begin(), threads.end(), [&](const Thread& thread) {
          return thread.id == entry.th32ThreadID;
        });
        if (known) continue;
        HANDLE handle = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ThreadID);
        if (!handle) continue;
        Thread thread;
        thread.id = entry.th32ThreadID;
        thread.handle = handle;
        thread.last_ms = ThreadMs(handle);
        threads.push_back(std::move(thread));
      }
      CloseHandle(snapshot);
      // Threads that exited: their time since the last call still counts, then they go.
      for (auto it = threads.begin(); it != threads.end();) {
        if (std::find(alive.begin(), alive.end(), it->id) != alive.end()) {
          ++it;
          continue;
        }
        const double ms = ThreadMs(it->handle);
        auto& slot = by_name[it->name.empty() ? "thread" : it->name];
        slot.first += ms > it->last_ms ? ms - it->last_ms : 0;
        CloseHandle(it->handle);
        it = threads.erase(it);
      }
      // Names may be set after a thread starts.
      for (Thread& thread : threads) thread.name = ThreadName(thread.handle);
    }
  }
  for (Thread& thread : threads) {
    const double ms = ThreadMs(thread.handle);
    if (ms < 0) continue;
    auto& slot = by_name[thread.name.empty() ? "thread" : thread.name];
    slot.first += ms > thread.last_ms ? ms - thread.last_ms : 0;
    ++slot.second;
    thread.last_ms = ms;
  }
  FILETIME created, exited, kernel, user;
  double process_ms = 0;
  if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
    process_ms = FiletimeMs(kernel) + FiletimeMs(user);
  const double window_ms = last_process_ms < 0 ? 0 : process_ms - last_process_ms;
  last_process_ms = process_ms;
  std::vector<std::pair<std::string, std::pair<double, int>>> top(by_name.begin(), by_name.end());
  std::sort(top.begin(), top.end(),
            [](const auto& a, const auto& b) { return a.second.first > b.second.first; });
  if (top.size() > 12) top.resize(12);
  output << ",\"process_cpu\":{\"ms\":" << window_ms << ",\"threads\":{";
  bool first = true;
  for (const auto& [name, value] : top) {
    std::string escaped;
    for (const char c : name) {
      if (c == '"' || c == '\\') escaped += '\\';
      if (static_cast<unsigned char>(c) >= 0x20) escaped += c;
    }
    output << (first ? "" : ",") << '"' << escaped << "\":[" << value.first << ',' << value.second
           << ']';
    first = false;
  }
  output << "}}";
}

std::mutex latest_mutex;
std::string latest;

// The sampler runs on a thread of its own at below normal priority: a thread
// snapshot of the system takes tens of milliseconds, which on the render thread
// held a frame every tenth window.
void SamplerMain() {
  SetThreadDescription(GetCurrentThread(), L"Perf Trace CPU");
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
  for (;;) {
    Sleep(1000);
    std::ostringstream sample;
    Sample(sample);
    std::lock_guard<std::mutex> lock(latest_mutex);
    latest = sample.str();
  }
}

}  // namespace

void WriteProcessCpu(std::ostream& output) {
  static std::once_flag started;
  std::call_once(started, [] { std::thread(SamplerMain).detach(); });
  std::lock_guard<std::mutex> lock(latest_mutex);
  output << latest;
}
#else
void WriteProcessCpu(std::ostream&) {}
#endif

}  // namespace rex::graphics::perf_trace
