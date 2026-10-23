// The producer side of the identity check of the native path, stage N1
// increment 2 of docs/analysis/d3d-hle.md (section 14.7), off by default. The
// hooks are in fm4-guest-hooks.cpp.
//
// FM4_NATIVE_CHECK=1: the hooked PM4 writers (the dirty state writers, the draw
// flush and the draw writers) note the device's ring write pointer (device+48,
// the last dword written) before the original body and copy the PM4 between it
// and the pointer after the body into rex/system/native_checks.h, keyed by the
// guest physical address of the first packet. Only the outermost hooked writer
// of a thread copies (the draw writers call the flush and the dirty writers).
// A ring allocation inside the writer (BeginCommand sub_823913D0,
// AllocateRingSpace sub_8234D738, the segment overflow sub_822FEF08) may move
// the write pointer to another command buffer segment, so it ends the range
// before the call and a new one starts after it; the PM4 the allocator writes
// itself is not copied. Nothing is copied while the device
// records a command buffer (device+13904): that PM4 is not in the ring. The
// runtime compares the copies with what it executes when host_render_check is
// set.
//
// The late write trace (with FM4_NATIVE_CHECK, unless FM4_LATE_WRITE_TRACE=0;
// docs/analysis/d3d-hle-n4.md 17.20): the last four dwords of the 1,024 latest copied ranges are kept, and
// of those the host render thread has not reached yet (their entry is pending),
// the hooks of the functions that may rewrite PM4 after its writer returned (the
// tiling bracket's EndTiling, its stream interpreter and drain, the list helper,
// the readers of the device's draw list, and the draw writer itself) compare
// them before and after the original body. A change is noted with the
// function, the range, the dword and the guest call chain (the link register
// and the saved link registers of the stack's back chain); the runtime's
// identity check prints the notes of a range whose PM4 it found changed, so the
// log stays quiet otherwise (most notes are of ranges that never run, whose
// memory the ring reuses). FM4_LATE_WRITE_TRACE=2 also prints every note.
#include "fm4-guest-hooks.h"

#include <rex/memory.h>
#include <rex/system/kernel_state.h>
#include <rex/system/native_checks.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

namespace fm4::hooks {
namespace {
uint32_t Load32(uint8_t* base, uint32_t address) {
  return rex::memory::load_and_swap<uint32_t>(base + address +
                                              (address >= 0xE0000000u ? 0x1000u : 0u));
}
// The XDK's virtual-to-physical conversion of command buffer pointers.
uint32_t Physical(uint32_t address) {
  return (address & 0x1FFFFFFF) + ((((address >> 20) + 0x200) & 0x1000));
}

bool Enabled() {
  static const bool enabled = [] {
    const char* value = std::getenv("FM4_NATIVE_CHECK");
    return value && *value && *value != '0';
  }();
  return enabled;
}

// The device, from the draw flush's first argument.
std::atomic<uint32_t> device{0};
std::atomic<uint64_t> sequence{0};

struct Range {
  uint32_t depth = 0;
  uint32_t writer = 0;
  uint32_t start = 0;  // device+48 when the range started.
  bool active = false;
  bool explicit_pointer = false;  // The writer passes its write pointer.
};
thread_local Range range;

// On with FM4_NATIVE_CHECK unless FM4_LATE_WRITE_TRACE=0.
int LateTraceLevel() {
  static const int level = [] {
    const char* value = std::getenv("FM4_LATE_WRITE_TRACE");
    return !Enabled() ? 0 : value && *value ? std::atoi(value) : 1;
  }();
  return level;
}

bool LateTraceEnabled() { return LateTraceLevel() > 0; }

// The latest copied ranges and their last four dwords as copied (guest order).
struct Recent {
  uint32_t first = 0;  // Guest physical address of the range's first dword.
  uint32_t dwords = 0;
  uint32_t writer = 0;
  uint64_t sequence = 0;
  std::array<uint32_t, 4> tail{};
};
constexpr uint32_t kRecent = 1024;
std::mutex recent_mutex;
std::array<Recent, kRecent> recent;
uint32_t recent_next = 0;

void Remember(uint32_t first, const rex::system::native_checks::Entry& entry) {
  Recent item;
  item.first = first;
  item.dwords = uint32_t(entry.dwords.size());
  item.writer = entry.writer;
  item.sequence = entry.sequence;
  const uint32_t count = std::min<uint32_t>(4, item.dwords);
  for (uint32_t i = 0; i < count; ++i) item.tail[i] = entry.dwords[item.dwords - count + i];
  std::lock_guard<std::mutex> lock(recent_mutex);
  recent[recent_next++ % kRecent] = item;
}

void Copy(uint8_t* base, uint32_t start, uint32_t end) {
  if (end == start) {
    return;
  }
  if (end < start || end - start > (1u << 20)) {
    rex::system::native_checks::CountDropped();
    return;
  }
  const uint32_t first = Physical(start + 4);
  const uint32_t dwords = (end - start) / 4;
  rex::system::native_checks::Entry entry;
  entry.writer = range.writer;
  entry.sequence = sequence.fetch_add(1, std::memory_order_relaxed);
  entry.dwords.resize(dwords);
  const uint8_t* source =
      rex::system::kernel_state()->memory()->TranslatePhysical<const uint8_t*>(first);
  std::memcpy(entry.dwords.data(), source, size_t(dwords) * 4);
  if (LateTraceEnabled()) Remember(first, entry);
  rex::system::native_checks::Record(first, std::move(entry));
}
}  // namespace

LateWriteSnapshot LateWriteCheckPre(uint32_t latest) {
  LateWriteSnapshot snapshot;
  if (!LateTraceEnabled()) return snapshot;
  std::vector<Recent> items;
  {
    std::lock_guard<std::mutex> lock(recent_mutex);
    latest = std::min(latest, kRecent);
    items.reserve(latest);
    for (uint32_t i = 1; i <= latest; ++i) items.push_back(recent[(recent_next - i) % kRecent]);
  }
  const uint8_t* memory = rex::system::kernel_state()->memory()->TranslatePhysical<const uint8_t*>(0);
  snapshot.entries.reserve(items.size());
  for (const Recent& item : items) {
    // Only ranges the host render thread has not reached: the ring reuses the
    // memory of executed ones.
    if (!item.dwords || !rex::system::native_checks::Pending(item.first, item.sequence)) continue;
    const uint32_t count = std::min<uint32_t>(4, item.dwords);
    LateWriteSnapshot::Entry entry;
    entry.first = item.first;
    entry.dwords = item.dwords;
    entry.writer = item.writer;
    entry.sequence = item.sequence;
    std::memcpy(entry.tail.data(), memory + item.first + (item.dwords - count) * 4, count * 4);
    snapshot.entries.push_back(entry);
  }
  return snapshot;
}

void LateWriteCheckPost(const LateWriteSnapshot& snapshot, uint32_t function, uint32_t lr,
                        uint32_t sp, uint32_t pcr, uint8_t* base) {
  if (snapshot.entries.empty()) return;
  const uint8_t* memory = rex::system::kernel_state()->memory()->TranslatePhysical<const uint8_t*>(0);
  for (const auto& entry : snapshot.entries) {
    const uint32_t count = std::min<uint32_t>(4, entry.dwords);
    std::array<uint32_t, 4> now{};
    std::memcpy(now.data(), memory + entry.first + (entry.dwords - count) * 4, count * 4);
    // Still waiting after the call: the change comes before the range runs.
    const bool pending = rex::system::native_checks::Pending(entry.first, entry.sequence);
    for (uint32_t i = 0; i < count; ++i) {
      if (now[i] == entry.tail[i]) continue;
      // The call chain: the hook's return address, then the saved link registers
      // of the back chain (the recompiled prologues store the link register at
      // -8 from the caller's frame).
      char chain[160] = {};
      int used = std::snprintf(chain, sizeof(chain), "%08X", lr);
      // Only inside the thread's stack (the PCR's stack base, the high end, at
      // +0x70 and its end, the low end, at +0x74).
      const uint32_t high = pcr ? Load32(base, pcr + 0x70) : 0;
      const uint32_t low = pcr ? Load32(base, pcr + 0x74) : 0;
      uint32_t frame = sp;
      for (int depth = 0; depth < 8 && used > 0 && used < int(sizeof(chain)) - 10; ++depth) {
        if (!high || frame < low || frame + 4 > high) break;
        const uint32_t back = Load32(base, frame);
        if (back <= frame || back > high || back - 8 < low) break;
        used += std::snprintf(chain + used, sizeof(chain) - used, " %08X", Load32(base, back - 8));
        frame = back;
      }
      const uint32_t index = entry.dwords - count + i;
      char text[400];
      std::snprintf(text, sizeof(text),
                    "%08X rewrote writer %08X range %08X (%u dwords, record %llu, %s) dword %u at "
                    "%08X: %08X -> %08X; chain %s",
                    function, entry.writer, entry.first, entry.dwords,
                    static_cast<unsigned long long>(entry.sequence),
                    pending ? "pending" : "taken during the call", index, entry.first + index * 4,
                    rex::byte_swap(entry.tail[i]), rex::byte_swap(now[i]), chain);
      if (LateTraceLevel() >= 2) std::fprintf(stderr, "FM4 late write: %s\n", text);
      rex::system::native_checks::NoteLateWrite(entry.sequence, text);
    }
  }
}

bool NativeCheckEnabled() { return Enabled(); }

void NativeCheckDevice(uint32_t address) {
  if (Enabled()) device.store(address, std::memory_order_relaxed);
}

void NativeCheckPre(uint32_t writer, uint8_t* base, uint32_t pointer) {
  if (!Enabled() || range.depth++) return;
  const uint32_t dev = device.load(std::memory_order_relaxed);
  range.writer = writer;
  range.active = dev && !Load32(base, dev + 13904);
  range.explicit_pointer = pointer != 0;
  if (range.active) range.start = pointer ? pointer : Load32(base, dev + 48);
}

void NativeCheckPost(uint8_t* base, uint32_t pointer) {
  if (!Enabled() || --range.depth) return;
  if (range.active) {
    Copy(base, range.start,
         pointer ? pointer : Load32(base, device.load(std::memory_order_relaxed) + 48));
    range.active = false;
  }
}

void NativeCheckSplitPre(uint8_t* base) {
  if (!Enabled() || !range.depth || !range.active) return;
  if (range.explicit_pointer) {
    // The writer's own pointer is not visible here: the range is not checked.
    rex::system::native_checks::CountDropped();
    range.active = false;
    return;
  }
  const uint32_t current = Load32(base, device.load(std::memory_order_relaxed) + 48);
  Copy(base, range.start, current);
  // Nested allocators (the overflow inside AllocateRingSpace) copy nothing twice.
  range.start = current;
}

void NativeCheckSplitPost(uint8_t* base) {
  if (!Enabled() || !range.depth || !range.active) return;
  range.start = Load32(base, device.load(std::memory_order_relaxed) + 48);
}

}  // namespace fm4::hooks
