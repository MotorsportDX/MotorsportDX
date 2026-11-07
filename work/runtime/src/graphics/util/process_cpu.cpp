#include "process_cpu.h"

#include <algorithm>
#include <atomic>
#include <array>
#include <cstring>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#include <rex/cvar.h>
#include <rex/platform.h>

#if REX_PLATFORM_WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <tlhelp32.h>
#endif

REXCVAR_DEFINE_BOOL(perf_trace_cpu, true, "GPU",
                    "With the perf trace: the process's CPU time and its threads' by name in each "
                    "window (process_cpu; docs/analysis/d3d-hle-n4.md 17.19)");
REXCVAR_DEFINE_INT32(perf_trace_heap_seconds, 0, "GPU",
                     "With the perf trace: every this many seconds, the busy blocks of the "
                     "process's heaps by size to <trace>.heap.jsonl (0 = off; it walks every "
                     "heap under its lock, which stalls allocating threads for a while)");
REXCVAR_DEFINE_BOOL(perf_trace_d3d12_objects, false, "GPU",
                    "With perf_trace_heap_seconds: count the Direct3D 12 objects created and "
                    "still alive by their two calling frames (hooks the device's methods)");
REXCVAR_DEFINE_STRING(perf_trace_driver_alloc_sizes, "", "GPU",
                      "With perf_trace_heap_seconds: block sizes (comma-separated) that the "
                      "Direct3D 12 driver allocates from a heap, counted by the first two calling "
                      "frames outside the driver, allocated and alive (patches the driver's "
                      "imports of HeapAlloc and HeapFree)");
REXCVAR_DEFINE_STRING(perf_trace_heap_sample_sizes, "", "GPU",
                      "With perf_trace_heap_seconds: block sizes (comma-separated) whose first "
                      "bytes are written for a few blocks, with the module of every pointer into "
                      "one (vtables name the object)");

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

int HeapSeconds() { return REXCVAR_GET(perf_trace_heap_seconds); }

namespace {

// The module and offset of an address inside a loaded image, else empty.
std::string ModuleOffset(uint64_t address) {
  HMODULE module = nullptr;
  if (!address || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                          GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                      reinterpret_cast<LPCWSTR>(address), &module)) {
    return {};
  }
  char path[MAX_PATH] = {};
  GetModuleFileNameA(module, path, MAX_PATH);
  const char* name = path;
  for (const char* c = path; *c; ++c) {
    if (*c == '\\' || *c == '/') name = c + 1;
  }
  std::ostringstream text;
  text << name << "+0x" << std::hex << (address - uint64_t(uintptr_t(module)));
  return text.str();
}

// Direct3D 12 objects by the two frames that created them and their kind: the
// count created and the count alive. Every object created through a hooked
// device method gets a private data interface whose final release (with the
// object) counts it as gone.
struct ObjectSite {
  uint64_t created = 0, live = 0;
};
std::mutex object_mutex;
std::map<std::array<uint64_t, 3>, ObjectSite> object_sites;

// {0b2c4b8e-5d55-4f5a-9b0b-6d7f3c1e2a41}
constexpr GUID kObjectTrackerGuid = {
    0x0b2c4b8e, 0x5d55, 0x4f5a, {0x9b, 0x0b, 0x6d, 0x7f, 0x3c, 0x1e, 0x2a, 0x41}};

class ObjectTracker final : public IUnknown {
 public:
  explicit ObjectTracker(const std::array<uint64_t, 3>& site) : site_(site) {}
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override {
    if (riid == __uuidof(IUnknown)) {
      AddRef();
      *object = static_cast<IUnknown*>(this);
      return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
  ULONG STDMETHODCALLTYPE Release() override {
    const ULONG references = --references_;
    if (!references) {
      {
        std::lock_guard<std::mutex> lock(object_mutex);
        --object_sites[site_].live;
      }
      delete this;
    }
    return references;
  }

 private:
  std::atomic<ULONG> references_{1};
  std::array<uint64_t, 3> site_;
};

void TrackObject(void** object, uint64_t kind) {
  if (!object || !*object) return;
  void* frames[2] = {};
  // Skip this function and the hook.
  RtlCaptureStackBackTrace(2, 2, frames, nullptr);
  const std::array<uint64_t, 3> site = {uint64_t(uintptr_t(frames[0])),
                                        uint64_t(uintptr_t(frames[1])), kind};
  ID3D12Object* d3d12_object = nullptr;
  if (FAILED(static_cast<IUnknown*>(*object)->QueryInterface(IID_PPV_ARGS(&d3d12_object)))) {
    return;
  }
  auto* tracker = new ObjectTracker(site);
  {
    std::lock_guard<std::mutex> lock(object_mutex);
    ObjectSite& counts = object_sites[site];
    ++counts.created;
    ++counts.live;
  }
  if (FAILED(d3d12_object->SetPrivateDataInterface(kObjectTrackerGuid, tracker))) {
    std::lock_guard<std::mutex> lock(object_mutex);
    --object_sites[site].live;
  }
  tracker->Release();
  d3d12_object->Release();
}

// The device's vtable slots (ID3D12Device) whose objects are counted.
enum : size_t {
  kSlotCommandAllocator = 9,
  kSlotGraphicsPipeline = 10,
  kSlotComputePipeline = 11,
  kSlotCommandList = 12,
  kSlotDescriptorHeap = 14,
  kSlotRootSignature = 16,
  kSlotCommittedResource = 27,
  kSlotHeap = 28,
  kSlotPlacedResource = 29,
  kSlotReservedResource = 30,
  kSlotFence = 36,
  kSlotQueryHeap = 39,
  kSlotCommandSignature = 41,
  kSlotCount = 42,
};
void* original_slots[kSlotCount] = {};

template <size_t kSlot, typename... Args>
HRESULT STDMETHODCALLTYPE HookCreate(ID3D12Device* device, Args... args) {
  using Function = HRESULT(STDMETHODCALLTYPE*)(ID3D12Device*, Args...);
  const HRESULT result = reinterpret_cast<Function>(original_slots[kSlot])(device, args...);
  if (SUCCEEDED(result)) {
    // The object is always the last argument.
    void** object = std::get<sizeof...(Args) - 1>(std::tuple<Args...>(args...));
    TrackObject(object, kSlot);
  }
  return result;
}

template <size_t kSlot, typename... Args>
void HookSlot(void** vtable) {
  original_slots[kSlot] = vtable[kSlot];
  vtable[kSlot] = reinterpret_cast<void*>(&HookCreate<kSlot, Args...>);
}

}  // namespace

namespace {

// Heap blocks of chosen sizes that the driver allocates, by the two first
// frames outside the driver: the count allocated and the count alive.
std::mutex driver_alloc_mutex;
std::map<std::array<uint64_t, 3>, ObjectSite> driver_alloc_sites;
std::map<uint64_t, std::array<uint64_t, 3>> driver_alloc_blocks;
std::vector<size_t> driver_alloc_sizes;
std::vector<std::pair<uint64_t, uint64_t>> driver_ranges;
using HeapAllocFunction = LPVOID(WINAPI*)(HANDLE, DWORD, SIZE_T);
using HeapFreeFunction = BOOL(WINAPI*)(HANDLE, DWORD, LPVOID);
HeapAllocFunction original_heap_alloc = nullptr;
// The pipeline that the replay is binding on this thread, its driver blocks by
// pipeline, and the pipelines it bound.
thread_local void* binding_pipeline = nullptr;
std::map<uint64_t, uint64_t> pipeline_blocks;
std::map<uint64_t, uint64_t> pipeline_binds;
HeapFreeFunction original_heap_free = nullptr;

bool InDriver(uint64_t address) {
  for (const auto& [start, end] : driver_ranges) {
    if (address >= start && address < end) return true;
  }
  return false;
}

LPVOID WINAPI DriverHeapAlloc(HANDLE heap, DWORD flags, SIZE_T bytes) {
  LPVOID block = original_heap_alloc(heap, flags, bytes);
  if (!block || std::find(driver_alloc_sizes.begin(), driver_alloc_sizes.end(), bytes) ==
                    driver_alloc_sizes.end()) {
    return block;
  }
  void* frames[32] = {};
  const USHORT count = RtlCaptureStackBackTrace(1, 32, frames, nullptr);
  std::array<uint64_t, 3> site = {0, 0, uint64_t(bytes)};
  size_t found = 0;
  for (USHORT i = 0; i < count && found < 2; ++i) {
    const uint64_t address = uint64_t(uintptr_t(frames[i]));
    if (!InDriver(address)) site[found++] = address;
  }
  if (!found && count >= 4) {
    // A thread of the driver or the runtime: the frames above its start
    // (RtlUserThreadStart and BaseThreadInitThunk) name it.
    site[0] = uint64_t(uintptr_t(frames[count - 3]));
    site[1] = uint64_t(uintptr_t(frames[count - 4]));
  }
  std::lock_guard<std::mutex> lock(driver_alloc_mutex);
  ObjectSite& counts = driver_alloc_sites[site];
  ++counts.created;
  ++counts.live;
  driver_alloc_blocks[uint64_t(uintptr_t(block))] = site;
  if (binding_pipeline) ++pipeline_blocks[uint64_t(uintptr_t(binding_pipeline))];
  return block;
}

BOOL WINAPI DriverHeapFree(HANDLE heap, DWORD flags, LPVOID block) {
  if (block) {
    std::lock_guard<std::mutex> lock(driver_alloc_mutex);
    auto it = driver_alloc_blocks.find(uint64_t(uintptr_t(block)));
    if (it != driver_alloc_blocks.end()) {
      --driver_alloc_sites[it->second].live;
      driver_alloc_blocks.erase(it);
    }
  }
  return original_heap_free(heap, flags, block);
}

uint64_t ModuleEnd(HMODULE module) {
  auto* base = reinterpret_cast<const uint8_t*>(module);
  auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
  return uint64_t(uintptr_t(module)) + nt->OptionalHeader.SizeOfImage;
}

// Replaces the module's imports of kernel32's HeapAlloc and HeapFree.
void PatchHeapImports(HMODULE module) {
  auto* base = reinterpret_cast<uint8_t*>(module);
  auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
  auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
  const IMAGE_DATA_DIRECTORY& directory =
      nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  if (!directory.VirtualAddress) return;
  for (auto* import = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress);
       import->Name; ++import) {
    if (_stricmp(reinterpret_cast<const char*>(base + import->Name), "kernel32.dll")) continue;
    auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(
        base + (import->OriginalFirstThunk ? import->OriginalFirstThunk : import->FirstThunk));
    auto* addresses = reinterpret_cast<IMAGE_THUNK_DATA*>(base + import->FirstThunk);
    for (; names->u1.AddressOfData; ++names, ++addresses) {
      if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
      const char* name =
          reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData)->Name;
      void* hook = nullptr;
      if (!strcmp(name, "HeapAlloc")) {
        original_heap_alloc = reinterpret_cast<HeapAllocFunction>(addresses->u1.Function);
        hook = reinterpret_cast<void*>(&DriverHeapAlloc);
      } else if (!strcmp(name, "HeapFree")) {
        original_heap_free = reinterpret_cast<HeapFreeFunction>(addresses->u1.Function);
        hook = reinterpret_cast<void*>(&DriverHeapFree);
      } else {
        continue;
      }
      DWORD old_protection;
      if (VirtualProtect(&addresses->u1.Function, sizeof(void*), PAGE_READWRITE,
                         &old_protection)) {
        addresses->u1.Function = ULONGLONG(uintptr_t(hook));
        VirtualProtect(&addresses->u1.Function, sizeof(void*), old_protection, &old_protection);
      }
    }
  }
}

void HookDriverHeap() {
  {
    std::istringstream list(REXCVAR_GET(perf_trace_driver_alloc_sizes));
    std::string item;
    while (std::getline(list, item, ',')) {
      if (!item.empty()) driver_alloc_sizes.push_back(size_t(std::stoull(item)));
    }
  }
  if (driver_alloc_sizes.empty()) return;
  // The frames of the driver, the Direct3D 12 runtime and the system are skipped.
  const char* driver_modules[] = {"nvwgf2umx.dll",  "nvldumdx.dll", "d3d12core.dll",
                                  "d3d12.dll",      "ucrtbase.dll", "kernelbase.dll",
                                  "ntdll.dll",      "kernel32.dll"};
  for (const char* name : driver_modules) {
    if (HMODULE module = GetModuleHandleA(name)) {
      driver_ranges.emplace_back(uint64_t(uintptr_t(module)), ModuleEnd(module));
    }
  }
  if (HMODULE driver = GetModuleHandleA("nvwgf2umx.dll")) {
    PatchHeapImports(driver);
  }
}

}  // namespace

void SetBindingPipeline(void* pipeline) {
  if (driver_alloc_sizes.empty()) return;
  binding_pipeline = pipeline;
  if (pipeline) {
    std::lock_guard<std::mutex> lock(driver_alloc_mutex);
    ++pipeline_binds[uint64_t(uintptr_t(pipeline))];
  }
}

void TrackD3D12Objects(void* device_pointer) {
  static std::once_flag heap_hooked;
  std::call_once(heap_hooked, [] { HookDriverHeap(); });
  if (!REXCVAR_GET(perf_trace_d3d12_objects) || !device_pointer) return;
  static std::once_flag hooked;
  std::call_once(hooked, [device_pointer] {
    void** vtable = *reinterpret_cast<void***>(device_pointer);
    DWORD old_protection;
    if (!VirtualProtect(vtable, kSlotCount * sizeof(void*), PAGE_READWRITE, &old_protection)) {
      return;
    }
    using Riid = REFIID;
    HookSlot<kSlotCommandAllocator, D3D12_COMMAND_LIST_TYPE, Riid, void**>(vtable);
    HookSlot<kSlotGraphicsPipeline, const D3D12_GRAPHICS_PIPELINE_STATE_DESC*, Riid, void**>(
        vtable);
    HookSlot<kSlotComputePipeline, const D3D12_COMPUTE_PIPELINE_STATE_DESC*, Riid, void**>(
        vtable);
    HookSlot<kSlotCommandList, UINT, D3D12_COMMAND_LIST_TYPE, ID3D12CommandAllocator*,
             ID3D12PipelineState*, Riid, void**>(vtable);
    HookSlot<kSlotDescriptorHeap, const D3D12_DESCRIPTOR_HEAP_DESC*, Riid, void**>(vtable);
    HookSlot<kSlotRootSignature, UINT, const void*, SIZE_T, Riid, void**>(vtable);
    HookSlot<kSlotCommittedResource, const D3D12_HEAP_PROPERTIES*, D3D12_HEAP_FLAGS,
             const D3D12_RESOURCE_DESC*, D3D12_RESOURCE_STATES, const D3D12_CLEAR_VALUE*, Riid,
             void**>(vtable);
    HookSlot<kSlotHeap, const D3D12_HEAP_DESC*, Riid, void**>(vtable);
    HookSlot<kSlotPlacedResource, ID3D12Heap*, UINT64, const D3D12_RESOURCE_DESC*,
             D3D12_RESOURCE_STATES, const D3D12_CLEAR_VALUE*, Riid, void**>(vtable);
    HookSlot<kSlotReservedResource, const D3D12_RESOURCE_DESC*, D3D12_RESOURCE_STATES,
             const D3D12_CLEAR_VALUE*, Riid, void**>(vtable);
    HookSlot<kSlotFence, UINT64, D3D12_FENCE_FLAGS, Riid, void**>(vtable);
    HookSlot<kSlotQueryHeap, const D3D12_QUERY_HEAP_DESC*, Riid, void**>(vtable);
    HookSlot<kSlotCommandSignature, const D3D12_COMMAND_SIGNATURE_DESC*, ID3D12RootSignature*,
             Riid, void**>(vtable);
    VirtualProtect(vtable, kSlotCount * sizeof(void*), old_protection, &old_protection);
  });
}

void WriteHeaps(std::ostream& output, double seconds) {
  std::vector<HANDLE> heaps(64);
  DWORD count = GetProcessHeaps(DWORD(heaps.size()), heaps.data());
  if (count > heaps.size()) {
    heaps.resize(count);
    count = GetProcessHeaps(count, heaps.data());
  }
  heaps.resize(std::min<size_t>(count, heaps.size()));
  output << "{\"t\":" << seconds << ",\"process_heap\":" << uintptr_t(GetProcessHeap())
         << ",\"heaps\":[";
  std::vector<size_t> sample_sizes;
  {
    std::istringstream list(REXCVAR_GET(perf_trace_heap_sample_sizes));
    std::string item;
    while (std::getline(list, item, ',')) {
      if (!item.empty()) sample_sizes.push_back(size_t(std::stoull(item)));
    }
  }
  constexpr size_t kSamplesPerSize = 4, kSampleQwords = 8;
  bool first_heap = true;
  for (HANDLE heap : heaps) {
    // The first qwords of a few blocks of each sampled size, copied under the
    // lock and resolved to modules after it (the loader lock is not taken under
    // a heap lock).
    std::map<size_t, std::vector<std::array<uint64_t, kSampleQwords>>> samples;
    // Busy blocks by exact size; the sizes with the most bytes are written.
    std::map<size_t, std::pair<uint64_t, uint64_t>> sizes;
    uint64_t busy_blocks = 0, busy_bytes = 0, free_bytes = 0, committed = 0;
    if (!HeapLock(heap)) continue;
    PROCESS_HEAP_ENTRY entry = {};
    while (HeapWalk(heap, &entry)) {
      if (entry.wFlags & PROCESS_HEAP_ENTRY_BUSY) {
        ++busy_blocks;
        busy_bytes += entry.cbData;
        auto& size = sizes[entry.cbData];
        ++size.first;
        size.second += entry.cbData;
        if (entry.cbData >= kSampleQwords * 8 &&
            std::find(sample_sizes.begin(), sample_sizes.end(), entry.cbData) !=
                sample_sizes.end()) {
          auto& blocks = samples[entry.cbData];
          if (blocks.size() < kSamplesPerSize) {
            blocks.emplace_back();
            std::memcpy(blocks.back().data(), entry.lpData, kSampleQwords * 8);
          }
        }
      } else if (entry.wFlags & PROCESS_HEAP_REGION) {
        committed += entry.Region.dwCommittedSize;
      } else if (!(entry.wFlags & PROCESS_HEAP_UNCOMMITTED_RANGE)) {
        free_bytes += entry.cbData;
      }
    }
    HeapUnlock(heap);
    std::vector<std::pair<uint64_t, size_t>> by_bytes;
    for (const auto& [size, counts] : sizes) by_bytes.emplace_back(counts.second, size);
    std::sort(by_bytes.rbegin(), by_bytes.rend());
    output << (first_heap ? "" : ",") << "{\"heap\":" << uintptr_t(heap)
           << ",\"busy_blocks\":" << busy_blocks << ",\"busy_mb\":" << busy_bytes / 1048576.0
           << ",\"free_mb\":" << free_bytes / 1048576.0
           << ",\"region_committed_mb\":" << committed / 1048576.0 << ",\"sizes\":[";
    for (size_t i = 0; i < std::min<size_t>(by_bytes.size(), 40); ++i) {
      const auto& counts = sizes[by_bytes[i].second];
      output << (i ? "," : "") << '[' << by_bytes[i].second << ',' << counts.first << ','
             << counts.second << ']';
    }
    output << "],\"samples\":[";
    bool first_sample = true;
    for (const auto& [size, blocks] : samples) {
      for (const auto& block : blocks) {
        output << (first_sample ? "" : ",") << "{\"size\":" << size << ",\"qwords\":[";
        for (size_t i = 0; i < kSampleQwords; ++i) {
          std::string module = ModuleOffset(block[i]);
          std::ostringstream value;
          value << "0x" << std::hex << block[i];
          output << (i ? "," : "") << '"' << (module.empty() ? value.str() : module) << '"';
        }
        output << "]}";
        first_sample = false;
      }
    }
    output << "]}";
    first_heap = false;
  }
  output << "]";
  if (REXCVAR_GET(perf_trace_d3d12_objects)) {
    // The sites with the most objects alive, then those with the most created.
    std::vector<std::pair<std::array<uint64_t, 3>, ObjectSite>> sites;
    {
      std::lock_guard<std::mutex> lock(object_mutex);
      sites.assign(object_sites.begin(), object_sites.end());
    }
    auto write_sites = [&](const char* name, auto key) {
      std::sort(sites.begin(), sites.end(),
                [&](const auto& a, const auto& b) { return key(a.second) > key(b.second); });
      output << ",\"" << name << "\":[";
      for (size_t i = 0; i < std::min<size_t>(sites.size(), 30); ++i) {
        const auto& [site, counts] = sites[i];
        output << (i ? "," : "") << "[\"" << ModuleOffset(site[0]) << "\",\""
               << ModuleOffset(site[1]) << "\"," << site[2] << ',' << counts.created << ','
               << counts.live << ']';
      }
      output << ']';
    };
    write_sites("d3d12_live", [](const ObjectSite& site) { return site.live; });
    write_sites("d3d12_created", [](const ObjectSite& site) { return site.created; });
  }
  if (!driver_alloc_sizes.empty()) {
    std::vector<std::pair<std::array<uint64_t, 3>, ObjectSite>> sites;
    {
      std::lock_guard<std::mutex> lock(driver_alloc_mutex);
      sites.assign(driver_alloc_sites.begin(), driver_alloc_sites.end());
    }
    std::sort(sites.begin(), sites.end(),
              [](const auto& a, const auto& b) { return a.second.live > b.second.live; });
    {
      // Pipelines bound by the replay; of them, how many have 1, 2, 3, 4-7,
      // 8-15 and 16 or more driver blocks from their binds.
      std::lock_guard<std::mutex> lock(driver_alloc_mutex);
      uint64_t histogram[6] = {}, binds = 0;
      for (const auto& [pipeline, blocks] : pipeline_blocks) {
        ++histogram[blocks >= 16 ? 5 : blocks >= 8 ? 4 : blocks >= 4 ? 3 : blocks - 1];
      }
      for (const auto& [pipeline, count] : pipeline_binds) binds += count;
      output << ",\"pipeline_blocks\":{\"bound\":" << pipeline_binds.size()
             << ",\"binds\":" << binds << ",\"with_blocks\":" << pipeline_blocks.size()
             << ",\"histogram\":[";
      for (int i = 0; i < 6; ++i) output << (i ? "," : "") << histogram[i];
      output << "]}";
    }
    output << ",\"driver_allocs\":[";
    for (size_t i = 0; i < std::min<size_t>(sites.size(), 40); ++i) {
      const auto& [site, counts] = sites[i];
      output << (i ? "," : "") << "[\"" << ModuleOffset(site[0]) << "\",\""
             << ModuleOffset(site[1]) << "\"," << site[2] << ',' << counts.created << ','
             << counts.live << ']';
    }
    output << ']';
  }
  output << "}\n";
}
#else
void WriteProcessCpu(std::ostream&) {}
int HeapSeconds() { return 0; }
void TrackD3D12Objects(void*) {}
void SetBindingPipeline(void*) {}
void WriteHeaps(std::ostream&, double) {}
#endif

}  // namespace rex::graphics::perf_trace
