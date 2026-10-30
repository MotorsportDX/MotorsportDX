#include "thread_sampler.h"

#include <rex/platform.h>

#if REX_PLATFORM_WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <timeapi.h>
#include <tlhelp32.h>
#pragma comment(lib, "winmm.lib")

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "perf_trace.h"

namespace rex::graphics::thread_sampler {
namespace {

constexpr uint32_t kMaxFrames = 32;
// A thread is busy when it used this share of the 100 ms between two checks.
constexpr double kBusyShare = 0.2;
// Threads that are not busy are sampled every this many periods.
constexpr uint32_t kIdleEvery = 10;

struct Thread {
  DWORD id = 0;
  HANDLE handle = nullptr;
  double last_cpu_ms = 0;
  bool busy = false;
};

double ThreadCpuMs(HANDLE handle) {
  FILETIME created, exited, kernel, user;
  if (!GetThreadTimes(handle, &created, &exited, &kernel, &user)) return -1;
  const auto ms = [](const FILETIME& time) {
    return double(uint64_t(time.dwHighDateTime) << 32 | time.dwLowDateTime) / 10000.0;
  };
  return ms(kernel) + ms(user);
}

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

class Sampler {
 public:
  Sampler(const char* path, uint32_t period_ms) : output_(path), period_ms_(period_ms) {}

  void Run() {
    SetThreadDescription(GetCurrentThread(), L"Thread Sampler");
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    timeBeginPeriod(1);
    const DWORD self = GetCurrentThreadId();
    uint64_t tick = 0;
    uint64_t last_list = 0, last_check = 0;
    for (;;) {
      Sleep(period_ms_);
      const uint64_t now = perf_trace::Now();
      if (tick == 0 || now - last_list >= 2'000'000'000ull) {
        List(self);
        last_list = now;
      }
      if (now - last_check >= 100'000'000ull) {
        const double window_ms = tick == 0 ? 100.0 : double(now - last_check) / 1e6;
        for (Thread& thread : threads_) {
          const double cpu_ms = ThreadCpuMs(thread.handle);
          thread.busy = cpu_ms >= 0 && cpu_ms - thread.last_cpu_ms >= kBusyShare * window_ms;
          if (cpu_ms >= 0) thread.last_cpu_ms = cpu_ms;
        }
        last_check = now;
      }
      for (Thread& thread : threads_) {
        if (!thread.busy && tick % kIdleEvery != 0) continue;
        Sample(thread, now);
      }
      ++tick;
      if (tick % 500 == 0) output_.flush();
    }
  }

 private:
  // Adds the threads started since the last list, with their names.
  void List(DWORD self) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return;
    const DWORD pid = GetCurrentProcessId();
    std::vector<DWORD> alive;
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL more = Thread32First(snapshot, &entry); more; more = Thread32Next(snapshot, &entry)) {
      if (entry.th32OwnerProcessID != pid || entry.th32ThreadID == self) continue;
      alive.push_back(entry.th32ThreadID);
      if (std::any_of(threads_.begin(), threads_.end(),
                      [&](const Thread& thread) { return thread.id == entry.th32ThreadID; }))
        continue;
      HANDLE handle = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT |
                                     THREAD_QUERY_LIMITED_INFORMATION,
                                 FALSE, entry.th32ThreadID);
      if (!handle) continue;
      Thread thread;
      thread.id = entry.th32ThreadID;
      thread.handle = handle;
      thread.last_cpu_ms = ThreadCpuMs(handle);
      threads_.push_back(thread);
    }
    CloseHandle(snapshot);
    for (auto it = threads_.begin(); it != threads_.end();) {
      if (std::find(alive.begin(), alive.end(), it->id) == alive.end()) {
        CloseHandle(it->handle);
        it = threads_.erase(it);
      } else {
        ++it;
      }
    }
    // Names may be set after a thread starts: write a name again when it changed.
    for (const Thread& thread : threads_) {
      std::string name = ThreadName(thread.handle);
      auto& known = names_[thread.id];
      if (known != name) {
        known = name;
        output_ << "T " << thread.id << ' ' << name << '\n';
      }
    }
  }

  void Sample(const Thread& thread, uint64_t now) {
    if (SuspendThread(thread.handle) == DWORD(-1)) return;
    CONTEXT context = {};
    context.ContextFlags = CONTEXT_FULL;
    uint32_t frame_count = 0;
    // No allocation while the target is suspended: it may hold the heap lock.
    if (GetThreadContext(thread.handle, &context)) {
      while (frame_count < kMaxFrames && context.Rip) {
        pcs_[frame_count++] = context.Rip;
        DWORD64 image_base = 0;
        PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(context.Rip, &image_base, nullptr);
        if (!function) {
          // Leaf function without unwind data: the return address is on top.
          context.Rip = *reinterpret_cast<const DWORD64*>(context.Rsp);
          context.Rsp += 8;
        } else {
          PVOID handler_data = nullptr;
          DWORD64 establisher_frame = 0;
          RtlVirtualUnwind(UNW_FLAG_NHANDLER, image_base, context.Rip, function, &context,
                           &handler_data, &establisher_frame, nullptr);
        }
      }
    }
    ResumeThread(thread.handle);
    if (!frame_count) return;
    std::string key(reinterpret_cast<const char*>(pcs_), frame_count * sizeof(uint64_t));
    auto it = stacks_.find(key);
    if (it == stacks_.end()) {
      it = stacks_.emplace(std::move(key), uint32_t(stacks_.size())).first;
      output_ << "S " << it->second;
      for (uint32_t i = 0; i < frame_count; ++i) output_ << ' ' << Describe(pcs_[i]);
      output_ << '\n';
    }
    const auto* state = perf_trace::Get();
    const uint64_t origin = state ? state->origin : origin_;
    char line[64];
    std::snprintf(line, sizeof(line), "%.1f %lu %u\n", double(now - origin) / 1e6,
                  static_cast<unsigned long>(thread.id), it->second);
    output_ << line;
  }

  std::string Describe(uint64_t pc) {
    HMODULE module = nullptr;
    char buffer[64];
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(pc), &module)) {
      std::snprintf(buffer, sizeof(buffer), "?+0x%llx", static_cast<unsigned long long>(pc));
      return buffer;
    }
    const uint64_t base = reinterpret_cast<uint64_t>(module);
    auto it = modules_.find(base);
    if (it == modules_.end()) {
      char name[MAX_PATH] = {};
      GetModuleFileNameA(module, name, MAX_PATH);
      std::string file(name);
      const size_t slash = file.find_last_of("\\/");
      it = modules_.emplace(base, slash == std::string::npos ? file : file.substr(slash + 1)).first;
    }
    std::snprintf(buffer, sizeof(buffer), "+0x%llx", static_cast<unsigned long long>(pc - base));
    return it->second + buffer;
  }

  std::ofstream output_;
  uint32_t period_ms_;
  uint64_t origin_ = perf_trace::Now();
  uint64_t pcs_[kMaxFrames] = {};
  std::vector<Thread> threads_;
  std::unordered_map<DWORD, std::string> names_;
  std::unordered_map<std::string, uint32_t> stacks_;
  std::map<uint64_t, std::string> modules_;
};

}  // namespace

void Start() {
  static std::once_flag started;
  std::call_once(started, [] {
    const char* path = std::getenv("REX_THREAD_PROFILE");
    if (!path || !*path) return;
    const char* period = std::getenv("REX_THREAD_PROFILE_PERIOD_MS");
    const uint32_t period_ms = period && std::atoi(period) > 0 ? uint32_t(std::atoi(period)) : 2;
    auto* sampler = new Sampler(path, period_ms);
    std::thread([sampler] { sampler->Run(); }).detach();
  });
}

}  // namespace rex::graphics::thread_sampler

#else

namespace rex::graphics::thread_sampler {
void Start() {}
}  // namespace rex::graphics::thread_sampler

#endif
