// A data write watch for diagnostics: finds which recompiled function writes one guest address,
// how often and from which call chain (docs/analysis/racing-wheels.md, stage 4: the writer of
// the steering torque).
//
// FM4WriteWatch(base, address) puts a hardware write breakpoint (debug register 0, four bytes)
// on the guest address's host location, on every thread of the process; threads that start
// later get it within half a second. Each write traps after the store: a vectored exception
// handler records the time, the thread, the session update count, the new value and the host
// call stack (return addresses relative to the executable, which fm4_rex.map turns into the
// recompiled sub_XXXXXXXX functions; host calls are guest calls, so the stack is the guest's
// call chain). A thread of its own writes the records to FM4_WRITE_WATCH_FILE:
//   w <ms> tid=<thread> u=<updates> v=<float> <rva> <rva> ...   one write, innermost frame first
// Without the variable nothing is armed. Windows only; a second address replaces the first.
#include <atomic>
#include <cstdint>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <tlhelp32.h>

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <unordered_map>

#include <rex/memory.h>

#include "fm4-diagnostic-state.h"

namespace {

constexpr int kFrames = 12;
constexpr uint32_t kEvents = 1u << 17;

struct Event {
  uint64_t ns;
  uint32_t thread;
  uint32_t updates;
  uint32_t value;
  uint32_t frames;
  uint32_t rva[kFrames];
};

Event* events = nullptr;
std::atomic<uint32_t> event_count{0};
std::atomic<uint32_t> events_ready[kEvents / 32];
std::atomic<uintptr_t> watched{0};
std::atomic<uint32_t> watched_guest{0};
uintptr_t image_base = 0;
uintptr_t image_end = 0;

uint64_t SteadyNs() {
  using namespace std::chrono;
  return uint64_t(duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count());
}

uint32_t Rva(uint64_t address) {
  return address >= image_base && address < image_end ? uint32_t(address - image_base) : 0;
}

LONG CALLBACK WatchHandler(PEXCEPTION_POINTERS info) {
  if (info->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) return EXCEPTION_CONTINUE_SEARCH;
  CONTEXT* context = info->ContextRecord;
  if (!(context->Dr6 & 1)) return EXCEPTION_CONTINUE_SEARCH;
  context->Dr6 = 0;
  const uint32_t index = event_count.fetch_add(1, std::memory_order_relaxed);
  if (index >= kEvents) return EXCEPTION_CONTINUE_EXECUTION;
  Event& event = events[index];
  event.ns = SteadyNs();
  event.thread = GetCurrentThreadId();
  event.updates = fm4_diagnostic_updates.load(std::memory_order_relaxed);
  const uintptr_t address = watched.load(std::memory_order_relaxed);
  event.value = address ? rex::memory::load_and_swap<uint32_t>(reinterpret_cast<uint8_t*>(address))
                        : 0;
  // The faulting frame, then its callers, by the unwind data.
  CONTEXT walk = *context;
  uint32_t frames = 0;
  while (frames < kFrames && walk.Rip) {
    event.rva[frames++] = Rva(walk.Rip);
    DWORD64 image = 0;
    PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(walk.Rip, &image, nullptr);
    if (!function) break;
    void* handler_data = nullptr;
    DWORD64 establisher = 0;
    RtlVirtualUnwind(UNW_FLAG_NHANDLER, image, walk.Rip, function, &walk, &handler_data,
                     &establisher, nullptr);
  }
  event.frames = frames;
  events_ready[index / 32].fetch_or(1u << (index % 32), std::memory_order_release);
  return EXCEPTION_CONTINUE_EXECUTION;
}

// Arms debug register 0 on every thread but this one that does not have it yet.
void Arm(uintptr_t address, std::unordered_map<DWORD, uintptr_t>& armed) {
  const DWORD self = GetCurrentThreadId(), process = GetCurrentProcessId();
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return;
  THREADENTRY32 entry{};
  entry.dwSize = sizeof(entry);
  DWORD ids[1024];
  size_t count = 0;
  for (BOOL ok = Thread32First(snapshot, &entry); ok && count < 1024;
       ok = Thread32Next(snapshot, &entry)) {
    if (entry.th32OwnerProcessID == process && entry.th32ThreadID != self) {
      ids[count++] = entry.th32ThreadID;
    }
  }
  CloseHandle(snapshot);
  for (size_t i = 0; i < count; ++i) {
    auto it = armed.find(ids[i]);
    if (it != armed.end() && it->second == address) continue;
    HANDLE thread = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME,
                               FALSE, ids[i]);
    if (!thread) continue;
    // No allocation while a thread is suspended: it may hold the heap's lock.
    if (SuspendThread(thread) != DWORD(-1)) {
      CONTEXT context{};
      context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
      if (GetThreadContext(thread, &context)) {
        context.Dr0 = address;
        // L0 on, RW0 = 01 (writes), LEN0 = 11 (four bytes).
        context.Dr7 = (context.Dr7 & ~DWORD64(0xF0003)) | 1 | (DWORD64(1) << 16) |
                      (DWORD64(3) << 18);
        SetThreadContext(thread, &context);
      }
      ResumeThread(thread);
    }
    CloseHandle(thread);
    armed[ids[i]] = address;
  }
}

void WatchThread(FILE* file) {
  std::unordered_map<DWORD, uintptr_t> armed;
  uint32_t written = 0;
  uint64_t last_arm = 0;
  uintptr_t logged = 0;
  for (;;) {
    const uintptr_t address = watched.load(std::memory_order_relaxed);
    if (address != logged) {
      logged = address;
      std::fprintf(file, "# watch %08x at %.3f\n", watched_guest.load(std::memory_order_relaxed),
                   double(SteadyNs()) * 1e-6);
    }
    const uint64_t now = SteadyNs();
    if (address && now - last_arm >= 500'000'000ull) {
      last_arm = now;
      Arm(address, armed);
    }
    const uint32_t count = std::min(event_count.load(std::memory_order_relaxed), kEvents);
    while (written < count &&
           (events_ready[written / 32].load(std::memory_order_acquire) >> (written % 32)) & 1) {
      const Event& event = events[written++];
      std::fprintf(file, "w %.3f tid=%u u=%u v=%.6f", double(event.ns) * 1e-6, event.thread,
                   event.updates, std::bit_cast<float>(event.value));
      for (uint32_t i = 0; i < event.frames; ++i) std::fprintf(file, " %x", event.rva[i]);
      std::fputc('\n', file);
    }
    std::fflush(file);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
}

}  // namespace

void FM4WriteWatch(uint8_t* base, uint32_t address) {
  static FILE* file = []() -> FILE* {
    const char* path = std::getenv("FM4_WRITE_WATCH_FILE");
    if (!path || !*path) return nullptr;
    FILE* out = std::fopen(path, "w");
    if (!out) return nullptr;
    HMODULE module = GetModuleHandleW(nullptr);
    image_base = reinterpret_cast<uintptr_t>(module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(image_base + dos->e_lfanew);
    image_end = image_base + nt->OptionalHeader.SizeOfImage;
    events = static_cast<Event*>(VirtualAlloc(nullptr, sizeof(Event) * kEvents,
                                              MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    AddVectoredExceptionHandler(1, WatchHandler);
    std::fprintf(out, "# image base %llx\n", static_cast<unsigned long long>(image_base));
    std::thread(WatchThread, out).detach();
    return out;
  }();
  if (!file || !events || (address & 3)) return;
  const uintptr_t host = reinterpret_cast<uintptr_t>(base + address);
  if (watched.load(std::memory_order_relaxed) == host) return;
  watched_guest.store(address, std::memory_order_relaxed);
  watched.store(host, std::memory_order_relaxed);
}

#else

void FM4WriteWatch(uint8_t*, uint32_t) {}

#endif
