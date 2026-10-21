// Stage N0 counters of docs/analysis/d3d-hle.md, off by default. The hooks are
// in fm4-guest-hooks.cpp.
//
// FM4_N0_TRACE=<file> writes one JSON line per second:
// - "frames": Swap calls;
// - "ib": replays by path, [calls, descriptors]: "inline" (RunCommandBuffer
//   writes one INDIRECT_BUFFER per descriptor into the current segment, when
//   bit 0x20 of the buffer's flags at +108 is clear), and the sub_826E4A28
//   calls (bit 0x20 set, and the swap): "deferred" (a tiling bracket is open,
//   device+13584), "copied" (device+11152 non-zero, the descriptors are copied
//   into the ring), "queued" (the swap's call with r7 set: a tag stream and a
//   DPC) and "direct" (it returns 0 and sub_826E4BE0 writes INDIRECT_BUFFER at
//   return address 0x826E4C20), each with its descriptor count;
// - "cb": BeginCommandBuffer, EndCommandBuffer, D3DCommandBuffer_CreateClone
//   and RunCommandBuffer calls, the distinct buffers replayed (a clone counts
//   as its source recording), replays of a recording ended in an earlier
//   frame ("reused") or in the same frame, the replays among those whose buffer
//   object is not the recording's but points at its first PM4 chunk
//   ("by_chunk") or, with FM4_N0_KEYCHECK, holds a copy of its PM4
//   ("by_content"), replays of buffers never seen ending ("unseen"), and
//   RunCommandBuffer calls on the other object kind (type 10);
// - "render": calls of the render thread's frame sub_822DB318 that called
//   Swap, with their total and longest time, and the calls without a Swap (the
//   loop also calls it while it has nothing to present) with their total time.
// <file>.summary is rewritten every 5 seconds with the cumulative tables: the
// number of replays each recording had before it was recorded again (or by the
// end), a histogram of the replays of buffers never seen ending and the 16 most
// replayed of them with their first words, flags, nodes and descriptors, the
// guest threads that called each D3D entry point, and the ring allocations
// (BeginCommand sub_823913D0, AllocateRingSpace sub_8234D738) and segment
// overflows (sub_822FEF08) by return address.
//
// FM4_N0_KEYCHECK=1 (with FM4_N0_TRACE) also hashes, when the draw flush
// sub_823436B8 writes them, the microcode of every IM_LOAD and the block of
// every LOAD_ALU_CONSTANT, keyed by the packet's guest physical address
// (rex/system/pm4_emissions.h; the runtime compares them at execution with
// REX_PM4_STATS), and hashes the PM4 of every buffer at EndCommandBuffer and
// at each replay: "cb" "first" (no hash before), "same" and "changed" (against
// the previous hash of that buffer).
#include <Windows.h>

#include "fm4-guest-hooks.h"

#include <rex/hash.h>
#include <rex/memory.h>
#include <rex/system/kernel_state.h>
#include <rex/system/pm4_emissions.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fm4::hooks {
namespace {
constexpr const char* kEntryNames[] = {
    "SetTexture",        "SetPixelShader",      "SetVertexShader",
    "SetDepthStencilSurface", "SetRenderTarget", "SetViewport",
    "SetScissorRect",    "SetStreamSource",     "Clear",
    "Resolve",           "DrawVertices",        "DrawIndexedVertices",
    "DrawVerticesUP",    "BeginVertices",       "BeginIndexedVertices",
    "Swap",              "RunCommandBuffer",    "CRenderEngine::RunCommandBuffer",
    "RecordCommandBuffers", "BeginCommandBuffer", "EndCommandBuffer",
    "BeginTiling",       "EndTiling",           "sub_826ED978",
    "sub_826EDCE8",      "sub_826EDFA8",        "sub_822DB318"};
static_assert(std::size(kEntryNames) == size_t(N0Entry::kCount));
constexpr const char* kAllocatorNames[] = {"BeginCommand", "AllocateRingSpace", "overflow"};

enum Path : uint32_t { kDeferred, kCopied, kQueued, kDirect, kInline, kPathCount };

uint32_t Load32(uint8_t* base, uint32_t address) {
  return rex::memory::load_and_swap<uint32_t>(base + address +
                                              (address >= 0xE0000000u ? 0x1000u : 0u));
}
// The XDK's virtual-to-physical conversion of command buffer pointers.
uint32_t Physical(uint32_t address) {
  return (address & 0x1FFFFFFF) + ((((address >> 20) + 0x200) & 0x1000));
}
const uint8_t* PhysicalHost(uint32_t physical) {
  return rex::system::kernel_state()->memory()->TranslatePhysical<const uint8_t*>(physical);
}

// Per-thread call counts of the entry points; the writer reads them.
struct ThreadCalls {
  uint32_t tid = 0;
  std::array<std::atomic<uint64_t>, size_t(N0Entry::kCount)> calls{};
};

struct Recording {
  uint32_t end_frame = 0;
  uint64_t replays = 0;
};

// A replayed buffer never seen ending, as read at its first replay (the summary
// writer must not read guest memory: the buffer may be freed by then).
struct Unseen {
  uint64_t runs = 0;
  uint32_t words[3]{}, flags = 0, nodes = 0, descriptors = 0, first = 0;
};

struct Trace {
  std::string path;
  bool keycheck = false;
  std::chrono::steady_clock::time_point start;
  std::atomic<uint32_t> frame{0};
  // Window counters, exchanged by the writer.
  std::atomic<uint64_t> frames{0};
  std::atomic<uint64_t> paths[kPathCount]{}, path_descriptors[kPathCount]{};
  std::atomic<uint64_t> begins{0}, ends{0}, clones{0}, runs{0}, runs_type10{0};
  std::atomic<uint64_t> runs_reused{0}, runs_same_frame{0}, runs_unseen{0}, runs_by_chunk{0};
  std::atomic<uint64_t> runs_by_content{0};
  std::atomic<uint64_t> runs_first{0}, runs_same{0}, runs_changed{0};
  std::atomic<uint64_t> emitted_shaders{0}, emitted_constants{0}, scan_errors{0};
  std::atomic<uint64_t> render_calls{0}, render_ns{0}, render_max_ns{0};
  std::atomic<uint64_t> render_idle_calls{0}, render_idle_ns{0};
  std::mutex mutex;  // Guards everything below.
  std::unordered_set<uint32_t> window_buffers;
  std::unordered_map<uint32_t, Recording> recordings;
  std::unordered_map<uint32_t, uint32_t> clone_sources;
  std::unordered_map<uint32_t, uint32_t> chunk_owners;  // First PM4 chunk -> recording.
  std::unordered_map<uint64_t, uint32_t> content_owners;  // PM4 hash -> recording (keycheck).
  std::unordered_map<uint32_t, Unseen> unseen;
  std::unordered_map<uint32_t, uint64_t> last_hash;  // Buffer -> PM4 hash (keycheck).
  std::map<uint64_t, uint64_t> lifetime_replays;  // Replays per closed recording -> recordings.
  std::map<uint64_t, std::pair<uint64_t, uint64_t>> allocations;  // allocator << 32 | lr.
  std::vector<ThreadCalls*> threads;
  std::atomic<uint32_t> render_tid{0};
};

void WriteSummary(Trace& trace, double seconds);

Trace* GetTrace() {
  static Trace* trace = [] {
    const char* path = std::getenv("FM4_N0_TRACE");
    if (!path || !*path) return static_cast<Trace*>(nullptr);
    auto* result = new Trace;
    result->path = path;
    const char* keycheck = std::getenv("FM4_N0_KEYCHECK");
    result->keycheck = keycheck && *keycheck && *keycheck != '0';
    result->start = std::chrono::steady_clock::now();
    std::thread([result] {
      for (uint32_t second = 1;; ++second) {
        std::this_thread::sleep_until(result->start + std::chrono::seconds(second));
        Trace& t = *result;
        const auto now = std::chrono::steady_clock::now();
        const double seconds = std::chrono::duration<double>(now - t.start).count();
        const auto take = [](std::atomic<uint64_t>& v) {
          return static_cast<unsigned long long>(v.exchange(0, std::memory_order_relaxed));
        };
        size_t distinct;
        {
          std::lock_guard<std::mutex> lock(t.mutex);
          distinct = t.window_buffers.size();
          t.window_buffers.clear();
        }
        char line[1024];
        std::snprintf(
            line, sizeof(line),
            "{\"t\":%.3f,\"steady_ms\":%.1f,\"frames\":%llu,"
            "\"ib\":{\"deferred\":[%llu,%llu],\"copied\":[%llu,%llu],\"queued\":[%llu,%llu],"
            "\"direct\":[%llu,%llu],\"inline\":[%llu,%llu]},"
            "\"cb\":{\"begin\":%llu,\"end\":%llu,\"clone\":%llu,\"run\":%llu,\"distinct\":%zu,\"reused\":%llu,"
            "\"same_frame\":%llu,\"by_chunk\":%llu,\"by_content\":%llu,\"unseen\":%llu,"
            "\"type10\":%llu,\"first\":%llu,"
            "\"same\":%llu,"
            "\"changed\":%llu},"
            "\"emitted\":{\"im_load\":%llu,\"load_alu_constant\":%llu,\"scan_errors\":%llu},"
            "\"render\":{\"calls\":%llu,\"ms\":%.3f,\"max_ms\":%.3f,\"idle_calls\":%llu,"
            "\"idle_ms\":%.3f}}\n",
            seconds,
            std::chrono::duration<double, std::milli>(now.time_since_epoch()).count(),
            take(t.frames), take(t.paths[kDeferred]), take(t.path_descriptors[kDeferred]),
            take(t.paths[kCopied]), take(t.path_descriptors[kCopied]), take(t.paths[kQueued]),
            take(t.path_descriptors[kQueued]), take(t.paths[kDirect]),
            take(t.path_descriptors[kDirect]), take(t.paths[kInline]),
            take(t.path_descriptors[kInline]), take(t.begins), take(t.ends), take(t.clones),
            take(t.runs),
            distinct, take(t.runs_reused), take(t.runs_same_frame), take(t.runs_by_chunk),
            take(t.runs_by_content),
            take(t.runs_unseen),
            take(t.runs_type10), take(t.runs_first), take(t.runs_same), take(t.runs_changed),
            take(t.emitted_shaders), take(t.emitted_constants), take(t.scan_errors),
            take(t.render_calls), double(take(t.render_ns)) / 1e6,
            double(take(t.render_max_ns)) / 1e6, take(t.render_idle_calls),
            double(take(t.render_idle_ns)) / 1e6);
        if (std::FILE* file = std::fopen(t.path.c_str(), "ab")) {
          std::fputs(line, file);
          std::fclose(file);
        }
        if (second % 5 == 0) WriteSummary(t, seconds);
      }
    }).detach();
    return result;
  }();
  return trace;
}

void WriteSummary(Trace& trace, double seconds) {
  std::ofstream output(trace.path + ".summary.tmp");
  output << "# seconds " << seconds << "\n";
  std::lock_guard<std::mutex> lock(trace.mutex);
  // Recordings still open count with the replays they have so far.
  std::map<uint64_t, uint64_t> lifetimes = trace.lifetime_replays;
  for (const auto& [buffer, recording] : trace.recordings) ++lifetimes[recording.replays];
  output << "# replays per recording: <replays> <recordings>\n";
  for (const auto& [replays, count] : lifetimes) output << "L " << replays << ' ' << count << "\n";
  std::vector<std::pair<uint64_t, uint32_t>> unseen;
  for (const auto& [buffer, info] : trace.unseen) unseen.emplace_back(info.runs, buffer);
  std::sort(unseen.rbegin(), unseen.rend());
  output << "# replayed, never seen ending (" << unseen.size()
         << " buffers): H <runs from> <buffers>, powers of two\n";
  std::map<uint64_t, uint64_t> histogram;
  for (const auto& [runs, buffer] : unseen) {
    uint64_t bin = 1;
    while (bin * 2 <= runs) bin *= 2;
    ++histogram[bin];
  }
  for (const auto& [bin, count] : histogram) output << "H " << bin << ' ' << count << "\n";
  output << "# U <buffer> <runs> <words +0 +4 +8> <flags +108> <nodes> <descriptors> <first "
            "descriptor dwords>, as at the first replay\n";
  for (size_t i = 0; i < unseen.size() && i < 16; ++i) {
    const Unseen& info = trace.unseen.at(unseen[i].second);
    char row[160];
    std::snprintf(row, sizeof(row), "U %08X %llu %08X %08X %08X %08X %u %u %u\n",
                  unseen[i].second, static_cast<unsigned long long>(info.runs), info.words[0],
                  info.words[1], info.words[2], info.flags, info.nodes, info.descriptors,
                  info.first);
    output << row;
  }
  output << "# render thread " << trace.render_tid.load()
         << "\n# entry calls per thread: T <entry> <thread id> <calls>\n";
  for (size_t e = 0; e < size_t(N0Entry::kCount); ++e) {
    for (const ThreadCalls* thread : trace.threads) {
      const uint64_t calls = thread->calls[e].load(std::memory_order_relaxed);
      if (calls) output << "T " << kEntryNames[e] << ' ' << thread->tid << ' ' << calls << "\n";
    }
  }
  output << "# ring allocations: A <allocator> <return address> <calls> <dwords>\n";
  for (const auto& [key, value] : trace.allocations) {
    char row[96];
    std::snprintf(row, sizeof(row), "A %s %08X %llu %llu\n", kAllocatorNames[key >> 32],
                  uint32_t(key), static_cast<unsigned long long>(value.first),
                  static_cast<unsigned long long>(value.second));
    output << row;
  }
  output.close();
  std::error_code error;
  std::filesystem::rename(trace.path + ".summary.tmp", trace.path + ".summary", error);
}

ThreadCalls* MyThread(Trace& trace) {
  thread_local ThreadCalls* mine = nullptr;
  if (!mine) {
    mine = new ThreadCalls;
    mine->tid = GetCurrentThreadId();
    std::lock_guard<std::mutex> lock(trace.mutex);
    trace.threads.push_back(mine);
  }
  return mine;
}

void Bump(std::atomic<uint64_t>& counter, uint64_t value = 1) {
  counter.fetch_add(value, std::memory_order_relaxed);
}

// PM4 of a recording: a list of nodes at +116 (next, descriptor count, then
// descriptors of a size in dwords in the low 24 bits and a physical address).
uint64_t HashRecording(uint8_t* base, uint32_t buffer) {
  uint64_t hash = 0;
  uint32_t node = Load32(base, buffer + 116);
  for (uint32_t guard = 0; node && guard < 4096; ++guard) {
    const uint32_t count = std::min(Load32(base, node + 4), 4096u);
    for (uint32_t i = 0; i < count; ++i) {
      const uint32_t dwords = Load32(base, node + 8 + 8 * i) & 0xFFFFFF;
      const uint32_t address = Load32(base, node + 12 + 8 * i) & 0x1FFFFFFF;
      if (dwords) hash = XXH3_64bits_withSeed(PhysicalHost(address), size_t(dwords) * 4, hash);
    }
    node = Load32(base, node);
  }
  return hash;
}

// Physical address of the first PM4 chunk of a buffer, 0 without one.
uint32_t FirstChunk(uint8_t* base, uint32_t buffer) {
  const uint32_t node = Load32(base, buffer + 116);
  return node && Load32(base, node + 4) ? Load32(base, node + 12) & 0x1FFFFFFF : 0;
}

// Packets written by the draw flush on this thread: the device and the segment
// write pointer (device+48, the last dword written) when the scan started.
struct Scan {
  uint32_t device = 0, start = 0;
  bool active = false;
};
thread_local Scan scan;
// Set by Swap inside the render frame zone of this thread.
thread_local bool render_swapped = false;

void ScanPackets(Trace& trace, uint8_t* base, uint32_t device, uint32_t start, uint32_t end) {
  if (end <= start || end - start > (1u << 20)) {
    if (end != start) Bump(trace.scan_errors);
    return;
  }
  const bool recorded = Load32(base, device + 13904) != 0;
  uint32_t address = start + 4;
  while (address <= end) {
    const uint32_t header = Load32(base, address);
    const uint32_t type = header >> 30;
    uint32_t dwords = 1;
    if (type == 0 || type == 3) dwords += ((header >> 16) & 0x3FFF) + 1;
    if (type == 1) dwords = 3;
    if (type == 3) {
      const uint32_t opcode = (header >> 8) & 0x7F;
      if (opcode == 0x27 && address + 8 <= end) {  // IM_LOAD
        const uint32_t source = Load32(base, address + 4) & ~3u & 0x1FFFFFFF;
        const uint32_t length = (Load32(base, address + 8) & 0xFFFF) * 4;
        rex::system::pm4_emissions::Record(
            rex::system::pm4_emissions::Kind::kShader, Physical(address),
            {XXH3_64bits(PhysicalHost(source), length), length, recorded});
        Bump(trace.emitted_shaders);
      } else if (opcode == 0x2F && address + 12 <= end) {  // LOAD_ALU_CONSTANT
        const uint32_t source = Load32(base, address + 4) & 0x1FFFFFFF;
        const uint32_t length = (Load32(base, address + 12) & 0xFFF) * 4;
        rex::system::pm4_emissions::Record(
            rex::system::pm4_emissions::Kind::kConstants, Physical(address),
            {XXH3_64bits(PhysicalHost(source), length), length, recorded});
        Bump(trace.emitted_constants);
      }
    }
    address += dwords * 4;
  }
  if (address != end + 4) Bump(trace.scan_errors);
}
}  // namespace

bool N0Enabled() { return GetTrace() != nullptr; }

void N0Thread(N0Entry entry) {
  Trace* trace = GetTrace();
  if (!trace) return;
  auto& counter = MyThread(*trace)->calls[size_t(entry)];
  counter.store(counter.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
}

void N0SwapPre() {
  Trace* trace = GetTrace();
  if (!trace) return;
  Bump(trace->frames);
  trace->frame.fetch_add(1, std::memory_order_relaxed);
  render_swapped = true;
}

void N0RunCommandBufferPre(const PPCContext& ctx, uint8_t* base) {
  Trace* trace = GetTrace();
  if (!trace) return;
  const uint32_t buffer = ctx.r4.u32;
  Bump(trace->runs);
  if (buffer && (Load32(base, buffer) & 0xF) == 10) {
    Bump(trace->runs_type10);
    return;
  }
  const uint32_t flags = Load32(base, buffer + 108);
  uint32_t nodes = 0, descriptors = 0;
  uint32_t node = Load32(base, buffer + 116);
  const uint32_t first_dwords = node ? Load32(base, node + 8) & 0xFFFFFF : 0;
  const uint32_t chunk = FirstChunk(base, buffer);
  for (; node && nodes < 4096; ++nodes) {
    descriptors += Load32(base, node + 4);
    node = Load32(base, node);
  }
  if (!(flags & 0x20)) {
    Bump(trace->paths[kInline]);
    Bump(trace->path_descriptors[kInline], descriptors);
  }
  const uint32_t frame = trace->frame.load(std::memory_order_relaxed);
  const uint64_t hash = trace->keycheck ? HashRecording(base, buffer) : 0;
  {
    std::lock_guard<std::mutex> lock(trace->mutex);
    if (trace->keycheck) {
      // Against the previous replay of this buffer, or its EndCommandBuffer.
      auto [last, first] = trace->last_hash.try_emplace(buffer, hash);
      if (first) {
        Bump(trace->runs_first);
      } else {
        Bump(last->second == hash ? trace->runs_same : trace->runs_changed);
        last->second = hash;
      }
    }
    uint32_t recording = buffer;
    if (auto clone = trace->clone_sources.find(buffer); clone != trace->clone_sources.end()) {
      recording = clone->second;
    }
    auto it = trace->recordings.find(recording);
    if (it == trace->recordings.end() && chunk) {
      // A buffer object that points at the PM4 of a recording (the header
      // copied elsewhere): attribute the replay to that recording.
      if (auto owner = trace->chunk_owners.find(chunk); owner != trace->chunk_owners.end()) {
        recording = owner->second;
        it = trace->recordings.find(recording);
        if (it != trace->recordings.end()) Bump(trace->runs_by_chunk);
      }
    }
    if (it == trace->recordings.end() && hash) {
      // A copy of a recording's PM4 (same bytes elsewhere), with keycheck.
      if (auto owner = trace->content_owners.find(hash); owner != trace->content_owners.end()) {
        recording = owner->second;
        it = trace->recordings.find(recording);
        if (it != trace->recordings.end()) Bump(trace->runs_by_content);
      }
    }
    trace->window_buffers.insert(recording);
    if (it == trace->recordings.end()) {
      Bump(trace->runs_unseen);
      Unseen& info = trace->unseen[buffer];
      if (!info.runs++) {
        info = {1,
                {Load32(base, buffer), Load32(base, buffer + 4), Load32(base, buffer + 8)},
                flags,
                nodes,
                descriptors,
                first_dwords};
      }
    } else {
      ++it->second.replays;
      Bump(it->second.end_frame < frame ? trace->runs_reused : trace->runs_same_frame);
    }
  }
}

void N0ClonePost(uint32_t source, uint32_t clone) {
  Trace* trace = GetTrace();
  if (!trace || !clone) return;
  Bump(trace->clones);
  std::lock_guard<std::mutex> lock(trace->mutex);
  trace->clone_sources[clone] = source;
}

void N0BeginCommandBufferPre(const PPCContext&, uint8_t*) {
  Trace* trace = GetTrace();
  if (!trace) return;
  Bump(trace->begins);
}

uint32_t N0EndCommandBufferPre(const PPCContext& ctx, uint8_t* base) {
  Trace* trace = GetTrace();
  if (!trace) return 0;
  Bump(trace->ends);
  return Load32(base, ctx.r3.u32 + 13904);
}

void N0EndCommandBufferPost(uint32_t recording, uint8_t* base) {
  Trace* trace = GetTrace();
  if (!trace || !recording) return;
  Recording entry;
  entry.end_frame = trace->frame.load(std::memory_order_relaxed);
  const uint64_t hash = trace->keycheck ? HashRecording(base, recording) : 0;
  const uint32_t chunk = FirstChunk(base, recording);
  std::lock_guard<std::mutex> lock(trace->mutex);
  if (trace->keycheck) {
    trace->last_hash[recording] = hash;
    if (hash) trace->content_owners[hash] = recording;
  }
  if (chunk) trace->chunk_owners[chunk] = recording;
  auto [it, inserted] = trace->recordings.try_emplace(recording, entry);
  if (!inserted) {
    ++trace->lifetime_replays[it->second.replays];
    it->second = entry;
  }
}

uint32_t N0IndirectPathPre(const PPCContext& ctx, uint8_t* base) {
  Trace* trace = GetTrace();
  if (!trace) return kPathCount;
  const uint32_t device = ctx.r3.u32;
  // The descriptor count (r5) in the high bits, the path taken so far in the low.
  const uint32_t count = std::min(ctx.r5.u32, 0xFFFFFFu) << 8;
  if (Load32(base, device + 13584)) return count | kDeferred;
  if (Load32(base, device + 11152)) return count | kCopied;
  return count | (ctx.r7.u32 ? kQueued : kDirect);
}

void N0IndirectPathPost(uint32_t state, const PPCContext& ctx) {
  Trace* trace = GetTrace();
  if (!trace || (state & 0xFF) >= kPathCount) return;
  // A zero result means the caller writes INDIRECT_BUFFER itself (the copy path
  // also ends there when device+11152 dropped to zero under its lock).
  const uint32_t path = ctx.r3.u32 ? (state & 0xFF) : kDirect;
  Bump(trace->paths[path]);
  Bump(trace->path_descriptors[path], state >> 8);
}

void N0RingAllocation(uint32_t allocator, const PPCContext& ctx) {
  Trace* trace = GetTrace();
  if (!trace) return;
  std::lock_guard<std::mutex> lock(trace->mutex);
  auto& entry = trace->allocations[uint64_t(allocator) << 32 | uint32_t(ctx.lr)];
  ++entry.first;
  entry.second += allocator < 2 ? ctx.r4.u32 : 0;
}

void N0FlushPre(const PPCContext& ctx, uint8_t* base) {
  Trace* trace = GetTrace();
  if (!trace || !trace->keycheck) return;
  scan = {ctx.r3.u32, Load32(base, ctx.r3.u32 + 48), true};
}

void N0FlushPost(uint8_t* base) {
  Trace* trace = GetTrace();
  if (!trace || !scan.active) return;
  ScanPackets(*trace, base, scan.device, scan.start, Load32(base, scan.device + 48));
  scan.active = false;
}

void N0OverflowPre(const PPCContext& ctx, uint8_t* base) {
  Trace* trace = GetTrace();
  if (!trace) return;
  N0RingAllocation(2, ctx);
  if (scan.active && scan.device == ctx.r3.u32) {
    ScanPackets(*trace, base, scan.device, scan.start, Load32(base, scan.device + 48));
  }
}

void N0OverflowPost(const PPCContext&, uint8_t* base) {
  if (scan.active) scan.start = Load32(base, scan.device + 48);
}

static uint64_t NowNs() {
  return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                      std::chrono::steady_clock::now().time_since_epoch())
                      .count());
}

uint64_t N0ZoneStart() {
  if (!GetTrace()) return 0;
  render_swapped = false;
  return NowNs();
}

void N0RenderFrameEnd(uint64_t start) {
  Trace* trace = GetTrace();
  if (!trace || !start) return;
  const uint64_t ns = NowNs() - start;
  if (!render_swapped) {
    Bump(trace->render_idle_calls);
    Bump(trace->render_idle_ns, ns);
    return;
  }
  Bump(trace->render_calls);
  Bump(trace->render_ns, ns);
  uint64_t max = trace->render_max_ns.load(std::memory_order_relaxed);
  while (ns > max && !trace->render_max_ns.compare_exchange_weak(max, ns)) {
  }
  trace->render_tid.store(GetCurrentThreadId(), std::memory_order_relaxed);
}

}  // namespace fm4::hooks
